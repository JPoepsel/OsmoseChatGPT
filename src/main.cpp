/*********************************************************************
  OSMOSE CONTROLLER – V3.0 CLEAN ADDITIVE (CFG ENABLED VERSION)

  100% deine Originaldatei
  nur additive Settings-Integration
*********************************************************************/

#define DEBUG_LEVEL 4

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Adafruit_PCF8574.h>
#include <PubSubClient.h>
#include <time.h>
#include "web.h"
#include "config_settings.h"


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
const char* WIFI_SSID="VanFranz";
const char* WIFI_PASS="5032650326";
const char* AP_SSID="osmose";
const char* AP_PASS="osmose123";
bool wifiConnected=false;

String lastErrorMsg = "";


// ================= MQTT =================
const char* MQTT_HOST="MyRasPi.local";
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);


// ================= NTP =================
const char* NTP_SERVER="pool.ntp.org";
const long GMT_OFFSET=3600;
const int  DST_OFFSET=3600;


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
#define FLUSH_TIMEOUT_MS 120000
#define TDS_LIMIT        50
#define TDS_MAX_ALLOWED  120
#define MAX_LITERS       50
#define HISTORY_SEC      3600
#define MAX_AFTERFLOW_TIME 0.5f   // Sekunden


#define PULSES_PER_LITER_IN   100
#define PULSES_PER_LITER_OUT  100


static bool lastSwitchState = false;   // merken für Flanke
uint32_t valveClosedTs = 0;



// ============================================================
// Helpers
// ============================================================
bool inActive(uint8_t p){ return digitalRead(p)==LOW; }

/* ===== CFG enabled (NEU) ===== */
float liters(uint32_t p){
  return p / CFG("pulsesPerLiterOut", PULSES_PER_LITER_OUT);
}

float litersIn(uint32_t p){
  return p / CFG("pulsesPerLiterIn", PULSES_PER_LITER_IN);
}

float rawToTds(int raw){
  float v = raw * 3.3f / 4095.0f;
  return (133.42f*v*v*v - 255.86f*v*v + 857.39f*v) * 0.5f;
}


// ============================================================
// Historie (ORIGINAL)
// ============================================================
float histTds[HISTORY_SEC];
uint16_t histIdx=0;
void historyAdd(float t){
  histTds[histIdx]=t;
  histIdx=(histIdx+1)%HISTORY_SEC;
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
  DBG_INFO("[OUT] ALL OFF\n");
  for(int i=0;i<8;i++) setOut(i,false);
}



// ============================================================
// Flow Counter (ORIGINAL)
// ============================================================
volatile uint32_t cntIn=0,cntOut=0;
void IRAM_ATTR isrIn(){cntIn++;}
void IRAM_ATTR isrOut(){cntOut++;}


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

  DBG_INFO("[WiFi] connecting...\n");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID,WIFI_PASS);

  uint32_t t=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t<10000) delay(200);

  if(WiFi.status()==WL_CONNECTED){

    wifiConnected=true;
    MDNS.begin(CFG("mDNSName","osmose"));
    mqtt.setServer(
      CFG("mqttHost",MQTT_HOST),
      CFG("mqttPort",1883)
    );

    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);

    DBG_INFO("[WiFi] STA OK\n");

  }else{

    DBG_INFO("[WiFi] AP MODE\n");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID,AP_PASS);
  }
}


// ============================================================
// LEDs (ORIGINAL)
// ============================================================
bool blinkFast(){ return (millis()/60)%2; }
bool blinkSlow(){ return (millis()/500)%2; }

void updateLEDs(int s){
  setOut(LedWLAN, wifiConnected?true:blinkSlow());
  setOut(LedBezug, s==3);
  setOut(LedError, s==4);
  bool sp=false;
  if(s==1) sp=blinkFast();
  else if(s==2) sp=blinkSlow();
  setOut(LedSpuelen,sp);
}


// ============================================================
// StateMachine (ORIGINAL + hooks ADD)
// ============================================================
enum State{IDLE,PREPARE,FLUSH,PRODUCTION,ERROR};
const char* sName[]={"IDLE","PREPARE","FLUSH","PRODUCTION","ERROR"};

State state=IDLE,lastState=IDLE;
uint32_t stateStart=0;
uint32_t prodStartCnt=0;

void setState(State s){

  if(state==s) return;

  if(s != ERROR)          // ⭐ wichtig
    lastErrorMsg = "";

  if(state==PRODUCTION && s!=PRODUCTION) prodEnd(cntIn,cntOut);
  if(state!=PRODUCTION && s==PRODUCTION) prodStart(0,cntIn,cntOut);

  DBG_INFO("[STATE] %s -> %s\n",sName[state],sName[s]);

  state=s;
  if(s != ERROR)
    lastErrorMsg = "";

  stateStart=millis();
}

void enterError(const char* m){
  DBG_ERR("!!! ERROR: %s !!!\n",m);
  lastErrorMsg = m;          // ⭐ merken
  setState(ERROR);
}

void hardResetToIdle()
{
  allOff();

  cntIn = 0;
  cntOut = 0;
  prodStartCnt = 0;
  valveClosedTs = millis();

  producedLiters = 0;   // wichtig für Limit-System

  setState(IDLE);
}




// ============================================================
// Setup (ORIGINAL)
// ============================================================
void setup(){

  Serial.begin(115200);
  delay(800);

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

  startWifi();
  webInit();
  configEnsureExists();
  configLoad();

}


// ============================================================
// Loop (ORIGINAL + ADD checks)
// ============================================================
void loop(){

  mqttReconnect();
  mqtt.loop();

  static bool firstBoot=true;
  if(firstBoot && CFG("autoStart",false)){
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
    setState(PREPARE);
  }

  // ===== Web Stop =====
  if(webStopRequest){
    webStopRequest=false;
    hardResetToIdle();
  }

  // ===== Manual switch start (0 -> MANU rising edge) =====
  bool manualNow = inActive(PIN_SMANU);

  if(state == IDLE && manualNow && !lastSwitchState){
    DBG_INFO("[START] manual switch\n");
    setState(PREPARE);
  }

  lastSwitchState = manualNow;


  historyAdd(tds);
  lastAdd(raw,tds);
  prodUpdate(tds,cntIn,cntOut);

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

    switch(state){

      case IDLE:
        // warten auf Start (Web / Manual / AutoStart)
      break;

      case PREPARE:
        setOut(Relay,true);
        setOut(WIn,true);
        if(millis()-stateStart>PREPARE_TIME_MS)
          setState(FLUSH);
        break;

      case FLUSH:
        setOut(OtoS,true);

        if (tds < CFG("tdsLimit",TDS_LIMIT)) {
          setOut(OtoS,false);
          prodStartCnt=cntOut;
          setState(PRODUCTION);
        }

        if(millis()-stateStart > CFG("flushTimeSec",FLUSH_TIMEOUT_MS/1000)*1000)
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

    /* ===== SETTINGS: production volume limit ===== */
    if(produced > CFG("maxProductionLiters", MAX_LITERS))
      enterError("Volume limit");

    /* ===== SETTINGS: runtime safety limit (HIER!) ===== */
    if(millis() - stateStart > CFG("maxRuntimeSec", 300) * 1000)
      enterError("Max runtime reached");

    break;
}
   case ERROR:
        allOff();
        break;
    }
  }


  // ===== MQTT =====
  static uint32_t lastMQTT=0;

  if(state!=lastState || millis()-lastMQTT>10000){

    mqttPublish(sName[state],tds,liters(cntOut-prodStartCnt));

    lastState=state;
    lastMQTT=millis();
  }

  updateLEDs(state);
  webLoop(tds, sName[state], liters(cntOut-prodStartCnt));



  delay(120);
}
