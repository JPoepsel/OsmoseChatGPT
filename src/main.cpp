/*********************************************************************
  OSMOSE CONTROLLER – V3.0 CLEAN ADDITIVE

  100% deine Originaldatei
  nur additive Settings-Integration
*********************************************************************/

#define DEBUG_LEVEL 2

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Adafruit_PCF8574.h>
#include <PubSubClient.h>
#include <time.h>
#include <SPIFFS.h>
#include "web.h"
#include "config_settings.h"
#include "history.h"
#include "settings.h"



// ================= DEBUG =================
#if DEBUG_LEVEL>=1
#define DBG_ERR(...)  Serial.printf(__VA_ARGS__)
#else
#define DBG_ERR(...)
#endif
#if DEBUG_LEVEL>=2
#define DBG_INFO(...) Serial.printf(__VA_ARGS__)
#else
#define DBG_INFO(...)
#endif
#if DEBUG_LEVEL>=3
#define DBG_DBG(...)  Serial.printf(__VA_ARGS__)
#else
#define DBG_DBG(...)
#endif
#if DEBUG_LEVEL>=4
#define DBG_TRACE(...) Serial.printf(__VA_ARGS__)
#else
#define DBG_TRACE(...)
#endif


// ================= WIFI =================
const char* AP_SSID="osmose";

bool wifiConnected=false;

String lastErrorMsg = "";


// ================= MQTT =================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);


// ================= NTP =================
const char* NTP_SERVER="pool.ntp.org";
const long GMT_OFFSET=3600;
const int  DST_OFFSET=3600;

static float producedLiters = 0.0f;
uint32_t productionStartMs = 0;   // ⭐ neu


static bool productionLockedManual = false;
static bool productionLockedAuto   = false;
static bool lastLowSwitch = false;
static bool autoBlocked = false;   // ⭐ verhindert Auto-Neustart nach Schutzlimit


// ================= PINMAP =================
#define PIN_TDS_ADC     2
#define PIN_WCOUNT_IN   3
#define PIN_WCOUNT_OUT  4
#define PIN_WLOW        5
#define PIN_WHIGH       21
#define PIN_WERROR      20
#define PIN_SAUTO       10
#define PIN_SMANU       8
#define PIN_I2C_SDA     6
#define PIN_I2C_SCL     7


// ================= PARAMETER =================
#define PREPARE_TIME_MS  10000
#define FLUSH_TIMEOUT_MS 12000
#define TDS_LIMIT        50
#define TDS_MAX_ALLOWED  120
#define MAX_LITERS       50
#define HISTORY_SEC      3600
#define MAX_AFTERFLOW_TIME 0.5f   // Sekunden


#define PULSES_PER_LITER_IN   100
#define PULSES_PER_LITER_OUT  100


static bool lastSwitchState = false;   // merken für Flanke
uint32_t valveClosedTs = 0;

enum State{IDLE,PREPARE,FLUSH,PRODUCTION,INFO,ERROR};
// ============================================================
// StateMachine (ORIGINAL + hooks ADD)
// ============================================================
const char* sName[]={"IDLE","PREPARE","FLUSH","PRODUCTION","INFO","ERROR"};

State state=IDLE,lastState=IDLE;
uint32_t stateStart=0;
uint32_t prodStartCnt=0;


// ============================================================
// Flow Counter (ORIGINAL)
// ============================================================
volatile uint32_t cntIn=0,cntOut=0;
void IRAM_ATTR isrIn(){cntIn++;}
void IRAM_ATTR isrOut(){cntOut++;}


// ============================================================
// Helpers
// ============================================================
bool inActive(uint8_t p){ return digitalRead(p)==LOW; }


float liters(uint32_t p){
  return p / settings.pulsesPerLiterOut;
}

float litersIn(uint32_t p){
  return p / settings.pulsesPerLiterIn;
}

float producedLitersSafe()
{
  int32_t diff = (int32_t)cntOut - (int32_t)prodStartCnt;
  if(diff < 0) diff = 0;
  return liters(diff);
}


float rawToTds(int raw){
  float v = raw * 3.3f / 4095.0f;
  return (133.42f*v*v*v - 255.86f*v*v + 857.39f*v) * 0.5f;
}


void prodAdd(float d)
{
  producedLiters += d;
}

bool prodCheckLimit(bool isManualMode)
{
  float maxProd = isManualMode ?
    settings.maxProductionManualLiters :
    settings.maxProductionAutoLiters;

  if(maxProd <= 0) return false;

  if(producedLiters >= maxProd)
  {
    if(isManualMode)
      productionLockedManual = true;
    else
      productionLockedAuto = true;

    return true;
  }

  return false;
}

void prodHandleResets(bool lowSwitch, bool switchOffToOn)
{
  if(productionLockedAuto && lastLowSwitch && !lowSwitch){
    producedLiters = 0;
    productionLockedAuto = false;
  }

  if(productionLockedManual && switchOffToOn){
    producedLiters = 0;
    productionLockedManual = false;
  }

  lastLowSwitch = lowSwitch;
}


// ============================================================
// ===== ADD: LAST VALUE BUFFER ================================
#define LAST_BUF 128
struct Sample{ uint16_t raw; float tds; };
Sample lastBuf[LAST_BUF];
uint16_t lastIdx=0;
void lastAdd(uint16_t r,float t){
  lastBuf[lastIdx]={r,t};
  lastIdx=(lastIdx+1)%LAST_BUF;
}


// ============================================================
// ===== ADD: Produktions-Historie =============================
#define PROD_HISTORY_BUF 64
struct ProdEntry{
  time_t startTs,endTs;
  float tMin,tMax,tSum;
  uint32_t tCnt;
  float lOut,lIn;
};
ProdEntry prodHist[PROD_HISTORY_BUF];
uint16_t prodIdx=0;
bool prodActive=false;
uint32_t histIn0=0,histOut0=0;
float totalOut=0,totalIn=0;

time_t nowTs(){ time_t t; time(&t); return t; }

void prodStart(float tds,uint32_t in,uint32_t out){
  auto &e=prodHist[prodIdx];
  e.startTs=nowTs();
  e.endTs=0;
  e.tMin=e.tMax=tds;
  e.tSum=tds; e.tCnt=1;
  histIn0=in; histOut0=out;
  prodActive=true;
}

void prodUpdate(float tds,uint32_t in,uint32_t out){
  if(!prodActive) return;
  auto &e=prodHist[prodIdx];
  if(tds<e.tMin) e.tMin=tds;
  if(tds>e.tMax) e.tMax=tds;
  e.tSum+=tds; e.tCnt++;
  e.lOut=liters(out-histOut0);
  e.lIn =litersIn(in-histIn0);
}

void prodEnd(uint32_t in,uint32_t out){
  if(!prodActive) return;
  auto &e=prodHist[prodIdx];
  e.endTs=nowTs();
  e.lOut=liters(out-histOut0);
  e.lIn =litersIn(in-histIn0);
  totalOut+=e.lOut;
  totalIn +=e.lIn;
  prodIdx=(prodIdx+1)%PROD_HISTORY_BUF;
  prodActive=false;
}


// ============================================================
// PCF8574 (ORIGINAL + ADD state mirror)
// ============================================================
Adafruit_PCF8574 pcf;

enum{
  WIn=0,OOut,OtoS,Relay,
  LedWLAN,LedSpuelen,LedBezug,LedError
};

const bool pinInvert[8]={true,true,true,true,true,true,true,true};

bool wInOn=false;

void setOut(uint8_t p,bool on){
   if(p==WIn){
    if(wInOn && !on)              // gerade geschlossen
      valveClosedTs = millis();   // Zeit merken
    wInOn = on;
  }
  pcf.digitalWrite(p,pinInvert[p]? !on:on);
}

void allOff(){
  // DBG_INFO("[OUT] ALL OFF\n");
  for(int i=0;i<8;i++) setOut(i,false);
}




// ============================================================
// MQTT (ORIGINAL)
// ============================================================

void mqttReconnect(){

  if(!wifiConnected) return;
  if(mqtt.connected()) return;

  static uint32_t lastTry = 0;

  if(millis() - lastTry < 5000) return;   // only every 5s
  lastTry = millis();

  DBG_INFO("[MQTT] try connect...");

  if(mqtt.connect("osmose")){
    DBG_INFO("[MQTT] connected");
  }else{
    DBG_ERR("[MQTT] failed rc=%d", mqtt.state());
  }
}


void mqttPublish(const char* s,float tds,float prod){
  if(!mqtt.connected()) return;
  char b[32];
  mqtt.publish("osmose/state",s);
  snprintf(b,32,"%.1f",tds);
  mqtt.publish("osmose/tds_ppm",b);
  snprintf(b,32,"%.2f",prod);
  mqtt.publish("osmose/produced_liters",b);
  DBG_INFO("[MQTT] publish %s tds=%.1f prod=%.2f\n",s,tds,prod);
}


// ============================================================
// WiFi (ORIGINAL + NTP ADD)
// ============================================================
void startWifi(){

  DBG_INFO("[WiFi] connecting to %s, %s \n", settings.wifiSSID.c_str(),settings.wifiPassword.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(
    settings.wifiSSID.c_str(),
    settings.wifiPassword.c_str()
  );


  uint32_t t=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t<10000) { DBG_INFO("."); delay(200); }

  if(WiFi.status()==WL_CONNECTED){

    wifiConnected=true;
    MDNS.begin(settings.mDNSName.c_str());
    mqtt.setServer(
      settings.mqttHost.c_str(),
      settings.mqttPort
    );


    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);

    DBG_INFO("[WiFi] STA OK\n");

  }else{

    DBG_INFO("[WiFi] AP MODE\n");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, settings.apPassword.c_str());

  }
}


// ============================================================
// LEDs (ORIGINAL)
// ============================================================
bool blinkFast(){ return (millis()/60)%2; }
bool blinkSlow(){ return (millis()/500)%2; }
bool blinkInfo(){ return (millis()%1000) < 100; } // 0.1s an

void updateLEDs(State s){
  setOut(LedWLAN, wifiConnected?true:blinkSlow());
  setOut(LedBezug, s==PRODUCTION);

  bool sp=false;
  if(s==PREPARE) sp=blinkFast();
  else if(s==FLUSH) sp=blinkSlow();
  setOut(LedSpuelen,sp);

  if(s==ERROR)
    setOut(LedError,true);
  else if(s==INFO)
    setOut(LedError,blinkInfo());
  else
    setOut(LedError,false);
}






void setState(State s)
{
  if(state == s) return;

  DBG_INFO("[STATE] %s -> %s\n", sName[state], sName[s]);

  /* ========= START Produktion ========= */
  if(state == FLUSH && s == PRODUCTION)
  {
    productionStartMs = millis();
    prodStart(0, cntIn, cntOut);
    historyStartProduction();
  }

  /* ========= ENDE Produktion ========= */
  if(prodActive)   // ⭐⭐⭐ ENTSCHEIDEND! ⭐⭐⭐
  {
    if(s != PRODUCTION)
    {
      char reasonBuf[20];

      if(lastErrorMsg.length())
        strncpy(reasonBuf, lastErrorMsg.c_str(), sizeof(reasonBuf)-1);
      else
        strcpy(reasonBuf, "Stopped");

      reasonBuf[sizeof(reasonBuf)-1] = 0;

      prodEnd(cntIn, cntOut);
      historyEndProduction(reasonBuf);
      cntIn  = 0;
      cntOut = 0;
    }
  }

  state = s;
  stateStart = millis();

  if(s != ERROR && s != INFO)
    lastErrorMsg = "";
}




void enterError(const char* m){
  DBG_ERR("!!! ERROR: %s !!!\n",m);
  lastErrorMsg = m;          // ⭐ merken
  setState(ERROR);
}

void enterInfo(const char* m){
  lastErrorMsg = m;
  setState(INFO);
}

void hardResetToIdle()
{
  allOff();

  cntIn = 0;
  cntOut = 0;
  prodStartCnt = 0;
  valveClosedTs = millis();

  producedLiters = 0;
  autoBlocked = false;


  setState(IDLE);   // ✅ nur das!
}




// ============================================================
// Setup (ORIGINAL)
// ============================================================
void setup(){

  Serial.begin(115200);
  delay(800);

  SPIFFS.begin(true);   // <<< nur hier!
  historyInit();


  DBG_INFO("BOOT 3.0\n");

  pinMode(PIN_WLOW,INPUT_PULLUP);
  pinMode(PIN_WHIGH,INPUT_PULLUP);
  pinMode(PIN_WERROR,INPUT_PULLUP);
  pinMode(PIN_SAUTO,INPUT_PULLUP);
  pinMode(PIN_SMANU,INPUT_PULLUP);

  attachInterrupt(PIN_WCOUNT_IN,isrIn,RISING);
  attachInterrupt(PIN_WCOUNT_OUT,isrOut,RISING);

  analogReadResolution(12);

  Wire.begin(PIN_I2C_SDA,PIN_I2C_SCL);
  pcf.begin(0x38);

  allOff();

  configEnsureExists();
  configLoad();
  settingsLoad();   // ⭐ zuerst laden

  startWifi();      // ⭐ erst danach benutzen
  webInit();
}


// ============================================================
// Loop (ORIGINAL + ADD checks)
// ============================================================
void loop(){

  mqttReconnect();
  mqtt.loop();

  static bool firstBoot=true;
  if(firstBoot && settings.autoStart){
    firstBoot=false;
    setState(PREPARE);
  }

  int raw=analogRead(PIN_TDS_ADC);
  float tds=rawToTds(raw);
  /* =========================================
     Web Start/Stop Requests
  ========================================= */

  // ===== Web Start =====
  if(webStartRequest){
    webStartRequest=false;
    autoBlocked=false;   // ⭐ hier!
    setState(PREPARE);
  }


  // ===== Web Stop =====
  if(webStopRequest){
    webStopRequest=false;
    hardResetToIdle();
  }

  // ===== Manual switch start (0 -> MANU rising edge) =====
 
  bool manualNow = inActive(PIN_SMANU);

  if(state == IDLE && manualNow && !lastSwitchState) {
    DBG_INFO("[START] manual switch\n");
    autoBlocked=false;   
    setState(PREPARE);
  }



  lastSwitchState = manualNow;


  lastAdd(raw,tds);
  prodUpdate(tds,cntIn,cntOut);

  historyAddSample(tds, producedLitersSafe());


  bool off=!inActive(PIN_SAUTO)&&!inActive(PIN_SMANU);

  // ===== ADD: SAFETY =====
  if(inActive(PIN_WERROR)) enterError("Water error");

  static uint32_t lastCntIn=0;
  if(!wInOn && cntIn!=lastCntIn) {
    if(millis() - valveClosedTs > (uint32_t)(MAX_AFTERFLOW_TIME*1000)){
      enterError("Flow while valve closed");
    }
  }
  lastCntIn=cntIn;

  if(cntIn > 50 && millis() - stateStart > 3000){
    float ratio=(float)cntOut/(float)cntIn;
    if(ratio<0.3f) enterError("Bad flow ratio");
  }

  DBG_DBG("STATE=%s raw=%d tds=%.1f in=%lu out=%lu\n",
          sName[state],raw,tds,cntIn,cntOut);


  // ===== OFF =====
  if(off){

    allOff();

    // ⭐ HARD RESET der Messlogik
    cntIn = 0;
    cntOut = 0;
    prodStartCnt = 0;
    valveClosedTs = millis();

    setState(IDLE);
  }



  // ===== StateMachine =====
  if(!off){

    switch(state) {

      case IDLE: {
        bool autoMode   = inActive(PIN_SAUTO);
        bool manualMode = inActive(PIN_SMANU);

        // ⭐ echter Auto-Start (nur wenn nicht blockiert)
        if(autoMode && !manualMode && !autoBlocked) 
          setState(PREPARE);
  
      }
      break;
      case PREPARE:
        setOut(Relay,true);
        setOut(WIn,true);
        if(millis()-stateStart>PREPARE_TIME_MS)
          setState(FLUSH);
        break;

      case FLUSH:
        setOut(OtoS,true);

        if (tds < settings.tdsLimit) {
          setOut(OtoS,false);
          prodStartCnt=cntOut;
          setState(PRODUCTION);
        }

       if(millis() - stateStart > settings.maxFlushTimeSec * 1000)
          enterError("Flush timeout");
        break;

      case PRODUCTION: {
        setOut(OOut,true);
  
        if(tds > TDS_MAX_ALLOWED)
          enterError("TDS too high");
  
        float produced = liters(cntOut - prodStartCnt);
        prodAdd(produced - producedLiters);

        bool isManualMode = inActive(PIN_SMANU);
        bool lowSwitch    = inActive(PIN_WLOW);

        if(prodCheckLimit(isManualMode)){
          setState(IDLE);
          break;
        }

        bool switchNow = digitalRead(PIN_SMANU) || digitalRead(PIN_SAUTO);
        bool switchOffToOn = (!lastSwitchState && switchNow);
        lastSwitchState = switchNow;

        prodHandleResets(lowSwitch, switchOffToOn);

        /* =====================================================
            MODE DEPENDENT LIMITS (AUTO vs MANUAL) ⭐ ADD
            ===================================================== */

        float maxProd = isManualMode ?
          settings.maxProductionManualLiters :
          settings.maxProductionAutoLiters;

        float maxRun = isManualMode ?
          settings.maxRuntimeManualSec :
          settings.maxRuntimeAutoSec;


        /* ===== Production limit ===== */
        if(maxProd > 0 && produced > maxProd) {

          if(isManualMode)
            enterInfo("Volume limit");
          else {
            autoBlocked = true;
            enterError("Volume limit");
          }
        }


        /* ===== Runtime limit ===== */
        if(maxRun > 0 && (millis() - stateStart) > maxRun * 1000) {

          if(isManualMode)
            enterInfo("Max runtime reached");
          else {
            autoBlocked = true;
            enterError("Max runtime reached");
          }
        }


        break;
      }
      case ERROR:
        allOff();
      break;
      case INFO:
       allOff();
      break;
    }
  }


  // ===== MQTT =====
  static uint32_t lastMQTT=0;

  if(state!=lastState || millis()-lastMQTT>10000){

    mqttPublish(sName[state],tds,producedLitersSafe());

    lastState=state;
    lastMQTT=millis();
  }

  updateLEDs(state);
  
  uint32_t runtimeSec = 0;
  if(state == PRODUCTION)
    runtimeSec = (millis() - productionStartMs) / 1000;
  webLoop(tds, sName[state], producedLitersSafe(), manualNow, runtimeSec);

}
