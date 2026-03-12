/*********************************************************************
  OSMOSE CONTROLLER – V3.0 CLEAN ADDITIVE

  100% deine Originaldatei
  nur additive Settings-Integration
*********************************************************************/
#define ESP_VERSION "ESP v3.9.0"

#define DEBUG_LEVEL 2

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Adafruit_PCF8574.h>
#include <PubSubClient.h>
#include <time.h>
#include <SPIFFS.h>
#include <DNSServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <WiFiClientSecure.h>



#include "web.h"
#include "config_settings.h"
#include "history.h"
#include "settings.h"


#define PUSHOVER_TOKEN "a17cuw3ujrekv8badbjk9f59i1o663"
#define PUSHOVER_USER  "u5if6n9see17t7c42ny8id7fqegtkv"

#define TDS_AVG_SAMPLES 8


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
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool wifiConnected=false;



// ================= MQTT =================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);


// ================= NTP =================
const char* NTP_SERVER="pool.ntp.org";
const long GMT_OFFSET=3600;
const int  DST_OFFSET=3600;

String lastErrorMsg = "";
static bool runtimeTimeoutActive = false;
static bool autoPauseBlink = false;

static uint32_t lastActuatorSwitchMs = 0;
static bool autoStartNotified = false;

uint32_t productionStartMs = 0;  

static bool autoBlocked = false;   // verhindert Auto-Neustart nach Schutzlimit
static bool productionEnded = false;
static char lastStopReason[20] = "";
static float currentFlowLpm = 0.0f;
static float lastProducedLiters = 0.0f;
static uint32_t flowLastCnt = 0;
static uint32_t flowLastT   = 0;

static uint32_t ratioStartCntIn  = 0;
static uint32_t ratioStartCntOut = 0;
static uint32_t ratioStartMs     = 0;



static float tdsBuf[TDS_AVG_SAMPLES];
static uint8_t tdsIdx = 0;
static uint8_t tdsCount = 0;
static float tdsSum = 0.0f;



// ================= PINMAP =================
#define PIN_TDS_ADC     2
#define PIN_WCOUNT_IN   3
#define PIN_WCOUNT_OUT  4
#define PIN_WHIGH       5
#define PIN_WLOW        21
#define PIN_WERROR      20
#define PIN_SAUTO       10
#define PIN_SMANU       8
#define PIN_I2C_SDA     6
#define PIN_I2C_SCL     7


// ---- Flow-Sicherheitsparameter ----
#define FLOW_CLOSED_GRACE_MS    1500   // Nachlauf nach Ventil-ZU ignorieren
#define FLOW_CLOSED_MAX_PULSES  10     // erlaubte Impulse danach
#define FLOW_CLOSED_WINDOW_MS   3000   // Zeitfenster für Bewertung

#define AUTOFLUSH_PRODUCT_INTERVAL_MS 5000  // Pulsperiode bei Atoflush in ms
#define AUTOFLUSH_PRODUCT_PULSE_MS     100  // Pulsdauer bei Autoflush


static bool lastSwitchState = false;   // merken für Flanke
uint32_t valveClosedTs = 0;

static uint32_t autoflushLastPulseMs = 0;  // Helper für Autoflus-Pulses
static bool autoflushPulseActive = false;

enum State{
  IDLE,PREPARE,AUTOFLUSH,PRODUCTION,POSTFLUSH,SERVICEFLUSH,INFO,ERROR
};
// ============================================================
// StateMachine (ORIGINAL + hooks ADD)
// ============================================================
const char* sName[]={
  "IDLE","PREPARE","AUTOFLUSH","PRODUCTION","POSTFLUSH","SERVICEFLUSH","INFO","ERROR"
};

State state=IDLE,lastState=IDLE;
uint32_t stateStart=0;
uint32_t prodStartCnt=0;

static float currentFlowInLpm = 0.0f;
static uint32_t flowInLastCnt = 0;
static uint32_t flowInLastT   = 0;

// ============================================================
// Flow Counter (ORIGINAL)
// ============================================================
volatile uint32_t cntIn=0,cntOut=0;
void IRAM_ATTR isrIn(){cntIn++;}
void IRAM_ATTR isrOut(){cntOut++;}

uint32_t lastServiceFlushMs = 0;



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



float rawToTds(int raw)
{
  // Rohwert → Spannung
  float v = raw * 3.3f / 4095.0f;

  // Spannung → TDS 
  float tds =
    (133.42f*v*v*v - 255.86f*v*v + 857.39f*v) * 0.5f;

  // --- Gleitender Mittelwert ---
  if(tdsCount < TDS_AVG_SAMPLES) {
    tdsBuf[tdsIdx] = tds;
    tdsSum += tds;
    tdsCount++;
  } else {
    tdsSum -= tdsBuf[tdsIdx];
    tdsBuf[tdsIdx] = tds;
    tdsSum += tds;
  }

  tdsIdx = (tdsIdx + 1) % TDS_AVG_SAMPLES;

  return tdsSum / tdsCount;
}


// ============================================================
// Mode Helper (ADD)
// ============================================================
const char* currentModeStr()
{
  bool autoMode   = inActive(PIN_SAUTO);
  bool manualMode = inActive(PIN_SMANU);

  if(manualMode) return "MANUAL";
  if(autoMode)   return "AUTO";
  return "OFF";
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
   if(p <= Relay) lastActuatorSwitchMs = millis();
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
// MQTT
// ============================================================
void mqttPublish(const char* stateName,
                 float tds,
                 float litersNow,
                 float flowLpm,
                 uint32_t runtimeSec)
{
  if(settings.mqttHost.length() == 0) return;   
  if(!mqtt.connected()) return;

  char buf[32];

  mqtt.publish("osmose/state", stateName);
  mqtt.publish("osmose/mode",  currentModeStr());

  snprintf(buf, sizeof(buf), "%.1f", tds);
  mqtt.publish("osmose/tds", buf);

  snprintf(buf, sizeof(buf), "%.2f", flowLpm);
  mqtt.publish("osmose/flow", buf);

  snprintf(buf, sizeof(buf), "%.2f", currentFlowInLpm);
  mqtt.publish("osmose/flowIn", buf);

  snprintf(buf, sizeof(buf), "%.2f", litersNow);
  mqtt.publish("osmose/liters", buf);

  snprintf(buf, sizeof(buf), "%lu", runtimeSec);
  mqtt.publish("osmose/runtimeSec", buf);

  mqtt.publish("osmose/msg", lastErrorMsg.c_str());
}

void mqttReconnect(float tds){
  if(settings.mqttHost.length() == 0) return; 
  if(!wifiConnected) return;
  if(mqtt.connected()) return;

  static uint32_t lastTry = 0;

  if(millis() - lastTry < 5000) return;   // only every 5s
  lastTry = millis();

  DBG_INFO("[MQTT] try connect...");

  if(mqtt.connect("osmose")){
    DBG_INFO("[MQTT] connected");
    mqttPublish(sName[state], tds, producedLitersSafe(), 0, 0);
  }else{
    DBG_ERR("[MQTT] failed rc=%d", mqtt.state());
  }
}


void sendPushover(String msg)
{
  if(!wifiConnected) {
    // lastErrorMsg = "PUSH: no wifi";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if(!client.connect("api.pushover.net", 443)) {
    // lastErrorMsg = "PUSH: connect failed";
    return;
  }

  String body =
    "token=" PUSHOVER_TOKEN
    "&user=" PUSHOVER_USER
    "&title=Osmose"
    "&message=" + msg;

  client.println("POST /1/messages.json HTTP/1.1");
  client.println("Host: api.pushover.net");
  client.println("Connection: close");
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.print  ("Content-Length: ");
  client.println(body.length());
  client.println();
  client.print(body);

  // --- HTTP-Status lesen ---
  uint32_t t0 = millis();
  while(!client.available() && millis() - t0 < 3000) delay(10);

  if(client.available()) {
    String line = client.readStringUntil('\n');
    // lastErrorMsg = "PUSH OK: " + line;
  } else {
    // lastErrorMsg = "PUSH: no response";
  }
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
    Serial.printf(
      "\n[WIFI]\nIP=%s\nGW=%s\nDNS=%s\nHOST=http://%s.local\n\n",
      WiFi.localIP().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(),
      WiFi.dnsIP().toString().c_str(),
      settings.mDNSName.c_str()
    );

    wifiConnected=true;
    MDNS.begin(settings.mDNSName.c_str());
    mqtt.setServer(
      settings.mqttHost.c_str(),
      settings.mqttPort
    );

    if(settings.mqttHost.length() == 0)
      DBG_INFO("[MQTT] disabled (no host)\n");


    configTime(GMT_OFFSET,DST_OFFSET,NTP_SERVER);

    DBG_INFO("[WiFi] STA OK\n");

  }else{
    DBG_INFO("[WiFi] AP MODE\n");

    WiFi.mode(WIFI_AP);
    if(settings.apPassword.length() == 0) {
      DBG_INFO("[WiFi] AP open\n");
      WiFi.softAP(AP_SSID);                 // ⭐ offen
    } else {
      DBG_INFO("[WiFi] AP WPA2\n");
      WiFi.softAP(AP_SSID, settings.apPassword.c_str());  // ⭐ geschützt
    }

    IPAddress apIP = WiFi.softAPIP();

    /* ⭐⭐⭐ Captive DNS: alles -> ESP */
    dnsServer.start(DNS_PORT, "*", apIP);
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
  else if(s==AUTOFLUSH || s==POSTFLUSH) sp=blinkSlow();
  setOut(LedSpuelen,sp);

  if(s==ERROR)
    setOut(LedError,true);
  else if(s==INFO || autoPauseBlink)   
    setOut(LedError,blinkInfo());
  else
    setOut(LedError,false);

}

void finalizeProductionIfRunning(const char* reason)
{
  if(state == PRODUCTION && !productionEnded) {
    lastProducedLiters = producedLitersSafe();
    productionEnded = true;
    strncpy(lastStopReason, reason, sizeof(lastStopReason)-1);
    lastStopReason[sizeof(lastStopReason)-1] = 0;
  }
}


void setState(State s)
{
  if(state == s) return;

  DBG_INFO("[%s] [STATE] %s -> %s\n", currentModeStr(), sName[state], sName[s]);
  
  //  Flow-Messung sauber zurücksetzen
  flowLastCnt = cntOut;
  flowLastT   = millis();
  currentFlowLpm = 0.0f;

  
  if(s == PRODUCTION) {
    runtimeTimeoutActive = false;
    ratioStartCntIn  = cntIn;
    ratioStartCntOut = cntOut;
    ratioStartMs     = millis();
  }

  /* Nutzung setzt Service-Timer zurück */
  if(s == PRODUCTION || s == PREPARE)
    lastServiceFlushMs = millis();

  /* ========= START Produktion ========= */
  if((state == AUTOFLUSH || state == PREPARE) && s == PRODUCTION) {
    prodStartCnt = cntOut;
    productionStartMs = millis();
    historyStartProduction(currentModeStr());
  }

  /* ========= ENDE Produktion ========= */
  if (state == PRODUCTION && s != PRODUCTION && !productionEnded) {
    lastProducedLiters = producedLitersSafe();   // 🔒 FINAL einfrieren
    productionEnded = true;
  }

  if(s == IDLE) {
    autoStartNotified = false;
  }

  state = s;
  stateStart = millis();

  if(s != ERROR && s != INFO && s != SERVICEFLUSH)
    lastErrorMsg = "";
}


void stopProduction(const char* reason)
{
  strcpy(lastStopReason, reason);

  if(state == PRODUCTION){
    lastProducedLiters = producedLitersSafe();  // 🔒 exakt einmal
    productionEnded = true;

    if(settings.postFlushEnabled)
      setState(POSTFLUSH);
    else
      setState(IDLE);
  }
}


void enterError(const char* m)
{
  // laufende Produktion sauber abschließen
  if(state == PRODUCTION || state == PREPARE) {
    lastProducedLiters = producedLitersSafe();
    productionEnded = false;   // ⬅ wichtig: wir schreiben JETZT
    strncpy(lastStopReason, m, sizeof(lastStopReason)-1);
    lastStopReason[sizeof(lastStopReason)-1] = 0;

    historyEndProduction(lastStopReason, lastProducedLiters);
    webNotifyHistoryUpdate();
  }

  DBG_ERR("[%s] !!! ERROR: %s !!!\n", currentModeStr(), m);
  lastErrorMsg = m;
  sendPushover(m);
 
  setState(ERROR);
}

void enterInfo(const char* m){
  DBG_INFO("[%s] INFO: %s\n", currentModeStr(), m);
  lastErrorMsg = m;
  setState(INFO);
}

void hardResetToIdle()
{
  allOff();
  valveClosedTs = millis();
  setState(IDLE);   // ✅ nur das!
}




// ============================================================
// Setup 
// ============================================================
void setup(){
  Serial.begin(115200);
  delay(800);
  SPIFFS.begin(true);  
  historyInit();
  DBG_INFO(ESP_VERSION); DBG_INFO("\n");
  const esp_partition_t* p = esp_ota_get_running_partition();
  Serial.printf("Running partition: %s\n", p->label);
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
  configLoad();
  settingsLoad();   // ⭐ zuerst laden
  startWifi();      // ⭐ erst danach benutzen
  webInit();
  lastSwitchState = inActive(PIN_SMANU); // aktuellen Schalterzustand als „bereits gedrückt“ merken. Damit gibt es keine Fake-Flanke.
}

String buildStatusLine(float tds)
{
  bool low  = inActive(PIN_WLOW);
  bool high = inActive(PIN_WHIGH);
  bool err  = inActive(PIN_WERROR);

  char buf[120];
  snprintf(buf, sizeof(buf),
           "IN:%lu OUT:%lu TDS:%.1f  L:%d H:%d E:%d",
           cntIn,
           cntOut,
           tds,
           low  ? 1 : 0,
           high ? 1 : 0,
           err  ? 1 : 0);

  return String(buf);
}


// ============================================================
// Loop (ORIGINAL + ADD checks)
// ============================================================
void loop(){

  int raw=analogRead(PIN_TDS_ADC);
  float tds=rawToTds(raw);
 
  if(!wifiConnected)
    dnsServer.processNextRequest();

  mqttReconnect(tds);
  mqtt.loop();

 
  // =====================================================
  // ⭐ GLOBAL Auto-Flankenerkennung (immer aktiv!)
  // =====================================================
  static bool lastAutoMode = false;
  
  bool autoModeNow = inActive(PIN_SAUTO);
  
  if(!lastAutoMode && autoModeNow) {
    autoBlocked = false;
    autoPauseBlink = false;
    lastErrorMsg = "";
    if(state == IDLE) stateStart = millis();  // sauberer Reset
  }

  lastAutoMode = autoModeNow;

  /* =========================================
     Web Start/Stop Requests
  ========================================= */

  // ===== Web Start =====
  if(webStartRequest){
    webStartRequest = false;

    autoBlocked = false;
    autoPauseBlink = false;
    lastErrorMsg = "";

    setState(PREPARE);
  }


  // ===== Web Stop =====
  if(webStopRequest){
    webStopRequest = false;
    bool manualMode = inActive(PIN_SMANU);
    // AUTO → blockieren
    if(!manualMode) {
      autoBlocked = true;
      autoPauseBlink = true;
    }
    // ----- Produktion läuft -----
    if(state == PRODUCTION) {
      finalizeProductionIfRunning("User stop");
      stopProduction("User stop");
    }
    // ----- Vorbereitung / Spülen -----
    else if(state == PREPARE || state == AUTOFLUSH) {
      lastProducedLiters = 0.0f;
      productionEnded = false;
      strcpy(lastStopReason, "User stop");
      setState(IDLE);   // kein PostFlush!
    }
  }

  // ===== Manual switch start (0 -> MANU rising edge) =====
  bool manualNow = inActive(PIN_SMANU);
  if(state == IDLE && manualNow && !lastSwitchState) {
    DBG_INFO("[START] manual switch\n");
    autoBlocked=false;   
    setState(PREPARE);
  }

  lastSwitchState = manualNow;

  float histLiters =
    (state == PRODUCTION) ? producedLitersSafe() : lastProducedLiters;

    historyAddSample2s(tds,
                       histLiters,
                       currentFlowLpm,
                       currentFlowInLpm);

  bool off=!inActive(PIN_SAUTO)&&!inActive(PIN_SMANU);
 
  // STOP muss auch im ERROR wirken
  if((webStopRequest || off) && state == ERROR) {
    webStopRequest = false;
    autoBlocked = false;
    autoPauseBlink = false;
    lastErrorMsg = "";
    setState(IDLE);
  }


  // ===== ADD: SAFETY =====
  // =====================================================
  // WERROR – entprellt / glitchfest
  // =====================================================
  static uint32_t werrorSince = 0;

  if(inActive(PIN_WERROR)) {
    if(werrorSince == 0) {
      werrorSince = millis();          // Beginn merken
    } else if(millis() - werrorSince > 100) {
      enterError("Water error");       // echter Fehler
    }
  } else {
    werrorSince = 0;                   // wieder ruhig
  }

  // =====================================================
  // Einlauf trotz geschlossenem Einlassventil (robust)
  // =====================================================
  static uint32_t lastCntIn = 0;
  static uint32_t closedInPulses = 0;
  static uint32_t closedInStartMs = 0;
  
  if(!wInOn) {   // Einlassventil ZU
  
    if(cntIn != lastCntIn) {
  
      uint32_t now = millis();
  
      // Nachlauf ignorieren
      if(now - valveClosedTs > FLOW_CLOSED_GRACE_MS) {
  
        if(closedInStartMs == 0) {
          closedInStartMs = now;
          closedInPulses  = 0;
        }
  
        closedInPulses += (cntIn - lastCntIn);
  
        if(closedInPulses > FLOW_CLOSED_MAX_PULSES &&
           now - closedInStartMs < FLOW_CLOSED_WINDOW_MS)
        {
          enterError("Inflow while inlet valve closed");
        }
      }
    }
  
  } else {
    // Ventil offen → Reset
    closedInPulses = 0;
    closedInStartMs = 0;
  }
  
  lastCntIn = cntIn;

  // =====================================================
  // Flow-Ratio-Überwachung (nur PRODUCTION, verzögert)
  // =====================================================
  if(state == PRODUCTION) {
    // erst nach Anlaufzeit bewerten
    if(millis() - ratioStartMs > 8000) {   // 8 s Anlauf
      uint32_t dIn  = cntIn  - ratioStartCntIn;
      uint32_t dOut = cntOut - ratioStartCntOut;
      // nur bewerten, wenn überhaupt Durchfluss da ist
      if(dIn > 30) {
        float ratio = (float)dOut / (float)dIn;
        if(ratio < 0.3f) {
          enterError("Bad flow ratio (<30%)");
        }
        // neues Fenster starten
        ratioStartCntIn  = cntIn;
        ratioStartCntOut = cntOut;
        ratioStartMs     = millis();
      }
    }
  }


  DBG_DBG("STATE=%s raw=%d tds=%.1f in=%lu out=%lu\n",
          sName[state],raw,tds,cntIn,cntOut);

  // ===== OFF =====
  if(off && state != SERVICEFLUSH){
    allOff();
    lastSwitchState = false; 

    // laufende Produktion sauber beenden
    if(state == PRODUCTION) {
      stopProduction("User stop");
    }
  
    autoPauseBlink = false;
    runtimeTimeoutActive = false;
    lastErrorMsg = "";
  
    if(state != IDLE) {
      setState(IDLE);
    }
  }


  // =====================================================
  // Schwimmer-Plausibilität (AUTO, alle States)
  // =====================================================
  if(state != ERROR) {
    bool autoMode = inActive(PIN_SAUTO);
    if(autoMode) {
      bool lowSwim  = inActive(PIN_WLOW);
      bool highSwim = inActive(PIN_WHIGH);
  
      // oben Wasser, unten trocken → unmöglich
      if(highSwim && !lowSwim) {
        enterError("Level sensor mismatch (upper only)");
      }
    }
  }


  if(settings.serviceFlushEnabled &&
     settings.serviceFlushIntervalSec > 0 &&
     state != PRODUCTION &&
     state != PREPARE &&
     state != AUTOFLUSH &&
     state != POSTFLUSH &&
     state != SERVICEFLUSH &&
     state != ERROR )
  {
    if(millis() - lastServiceFlushMs >
       settings.serviceFlushIntervalSec * 1000)
    {
      setState(SERVICEFLUSH);
    }
  }
  // ===== StateMachine =====
  if(!off || state == SERVICEFLUSH){
  
    switch(state) {
  
      /* ===================================================== */
      case IDLE: {
       
        // Produktion physikalisch abgeschlossen → History schreiben
        if (productionEnded) {
          productionEnded = false;

          historyEndProduction(lastStopReason, lastProducedLiters);
          webNotifyHistoryUpdate();
        }
        
        setOut(Relay,false);
        setOut(WIn,false);
        setOut(OOut,false);
        setOut(OtoS,false);


        bool autoMode   = inActive(PIN_SAUTO);
        bool manualMode = inActive(PIN_SMANU);

        bool lowSwim  = inActive(PIN_WLOW);   // schwimmt = true
        bool highSwim = inActive(PIN_WHIGH);  // schwimmt = true
               
        /* ========= AUTO START nur wenn beide NICHT schwimmen ========= */
        if(autoMode && !manualMode && !autoBlocked)
        {
          if(!lowSwim && !highSwim) { // trocken unten
            if(!autoStartNotified) {
              sendPushover("Osmose Auto-Bezug gestartet");
              autoStartNotified = true;
            }
            setState(PREPARE);
          }
        }
        
        break;
      }
  
      /* ===================================================== */
      case PREPARE: {
        // Nur Wasser an, noch kein Produkt
        setOut(Relay,true);
        setOut(WIn,true);
        setOut(OOut,false);
        setOut(OtoS,false);
  
        if(millis()-stateStart > settings.prepareTimeSec * 1000) {
          if(settings.autoFlushEnabled)
            setState(AUTOFLUSH);
          else
            setState(PRODUCTION);
        }
        break;
      }

  
      /* ===================================================== */
      /* ===================================================== */
      case AUTOFLUSH: {
        // Grundzustand: Spülen zur Drain
        setOut(Relay,true);
        setOut(WIn,true);
        setOut(OtoS,true);

        uint32_t now = millis();

        /* ---------- Produkt-Puls starten ---------- */
        if(!autoflushPulseActive && now - autoflushLastPulseMs >= AUTOFLUSH_PRODUCT_INTERVAL_MS) {
          autoflushPulseActive = true;
          autoflushLastPulseMs = now;
        }

        /* ---------- Produkt-Puls aktiv ---------- */
        if(autoflushPulseActive) {
          setOut(OOut,true);   // kurz Produkt freigeben

          if(now - autoflushLastPulseMs >= AUTOFLUSH_PRODUCT_PULSE_MS) {
            autoflushPulseActive = false;
            setOut(OOut,false);  // wieder sperren
          }
        } else {
          setOut(OOut,false);
        }

        /* ---------- Mindest-Spülzeit ---------- */
        bool minFlushDone =
          (millis() - stateStart) >=
          (uint32_t)(settings.autoFlushMinTimeSec * 1000.0f);

        /* ---------- TDS-Entscheidung nur außerhalb des Pulses ---------- */
        if(minFlushDone && !autoflushPulseActive && tds < settings.tdsLimit) {
          // Puls-Status sauber zurücksetzen
          autoflushPulseActive = false;
          autoflushLastPulseMs = 0;

          prodStartCnt = cntOut;
          setState(PRODUCTION);
          break;
        }

        /* ---------- Absolute Schutzzeit ---------- */
        if(millis() - stateStart > settings.maxFlushTimeSec * 1000.0f) {
          enterError("Flush timeout");
          break;
        }

        break;
      }
      /* ===================================================== */
      case PRODUCTION: {

        setOut(Relay,true);
        setOut(WIn,true);
        setOut(OOut,true);
        setOut(OtoS,false);
      
        bool autoMode   = inActive(PIN_SAUTO);
        bool manualMode = inActive(PIN_SMANU);
      
        bool lowSwim  = inActive(PIN_WLOW);
        bool highSwim = inActive(PIN_WHIGH);
      
      
        /* =========================================================
           AUTO STOP → Tank voll (unverändert)
           ========================================================= */
        if(autoMode && !manualMode)
        {
          if(lowSwim && highSwim)
          {
            enterInfo("Container full");
            strcpy(lastStopReason, "Container full");

            if(settings.postFlushEnabled)
              setState(POSTFLUSH);
            else
              setState(IDLE);
      
            break;
          }
        }
      
      
        /* =========================================================
           TDS Limit
           ========================================================= */
        if(state == PRODUCTION &&  millis() - lastActuatorSwitchMs > 500 && tds > settings.tdsMaxAllowed) {
          enterError("TDS too high");
        }
      
        /* =========================================================
           Production tracking
           ========================================================= */
        bool isManualMode = manualMode;
            
        /* =========================================================
           ⭐ LITER LIMIT
           MANUAL → normal fertig
           AUTO   → ERROR
           ========================================================= */
        float maxProd = isManualMode ?
            settings.maxProductionManualLiters :
            settings.maxProductionAutoLiters;
      
        if(maxProd > 0 && producedLitersSafe() > maxProd)
        {
          if (isManualMode) {
            strcpy(lastStopReason, "Volume limit");
            // normaler Abschluss
            if(settings.postFlushEnabled)
              setState(POSTFLUSH);
            else
              setState(IDLE);
          } else {
            autoBlocked = true;
            enterError("Volume limit");
          }
          break;
        }
           
        /* =========================================================
           ⭐ RUNTIME LIMIT
           MANUAL → INFO
           AUTO   → ERROR
           ========================================================= */
        float maxRun = isManualMode ?
            settings.maxRuntimeManualSec :
            settings.maxRuntimeAutoSec;
      
        if(maxRun > 0 && (millis() - stateStart) > maxRun * 1000) {
          if(isManualMode) {
            runtimeTimeoutActive = true;
            strcpy(lastStopReason, "Max runtime reached");
     
            if(settings.postFlushEnabled)
              setState(POSTFLUSH);
            else
              setState(INFO);
          } else {
            autoBlocked = true;
            enterError("Max runtime reached");
          }
          break;
        }
        break;
      }
 
      /* ===================================================== */
      case POSTFLUSH: {
        // Postflush: Rohwasser spült, Osmose hydraulisch gesperrt
        setOut(Relay,true);   // Anlage aktiv
        setOut(WIn,true);     // Rohwasser an
        setOut(OOut,false);   // kein Produkt
        setOut(OtoS,false);   // Osmose vollständig gesperrt
        if(millis() - stateStart > settings.postFlushTimeSec * 1000) {
          if(runtimeTimeoutActive) {
            runtimeTimeoutActive = false;
            enterInfo("Max runtime reached");
          } else {
            setState(IDLE);
          }
        }
        break;
      }
      
      case SERVICEFLUSH: {
        /* nur Spülventil */
        setOut(Relay,true);   // Anlage aktiv
        setOut(WIn,true);     // Wasser rein
        setOut(OtoS,true);    // Spülweg
        setOut(OOut,false);   // KEIN Produktwasser
     
        if(millis() - stateStart > settings.serviceFlushTimeSec * 1000) {
          setOut(OtoS,false);
          lastServiceFlushMs = millis();
          /* optional Postflush nach ServiceFlush */
          if(settings.postFlushEnabled) {
            setState(POSTFLUSH);
          } else {
            if(lastErrorMsg.length())   // wenn Info aktiv war, diese nach dem Flush wiederherstellen
              setState(INFO);           
            else
              setState(IDLE);
          }
        }
      
        break;
      }

      /* ===================================================== */
      case ERROR:
      case INFO:
        // Nur hier komplett aus!
        allOff();
      break;
    }
  }

  // ===== MQTT =====
  static uint32_t lastMQTT = 0;
  uint32_t runtimeSec = 0;
  if(state == PRODUCTION)
    runtimeSec = (millis() - productionStartMs) / 1000;
  
  
  /* ---------- Flow-Berechnung (nur PRODUCTION) ---------- */
  uint32_t now = millis();
  if(state == PRODUCTION) {
    uint32_t dt = now - flowLastT;
    if(dt >= 3000) {
      // OUT
      uint32_t pulsesOut = cntOut - flowLastCnt;
      float dlOut = liters(pulsesOut);
      currentFlowLpm = (dlOut > 0)
        ? (dlOut / (dt / 1000.0f)) * 60.0f
        : 0.0f;
      flowLastCnt = cntOut;
      flowLastT   = now;
      // IN
      uint32_t pulsesIn = cntIn - flowInLastCnt;
      float dlIn = litersIn(pulsesIn);
      currentFlowInLpm = (dlIn > 0)
        ? (dlIn / (dt / 1000.0f)) * 60.0f
        : 0.0f;
      flowInLastCnt = cntIn;
      flowInLastT   = now;
    }
  } else {
    currentFlowLpm   = 0.0f;
    currentFlowInLpm = 0.0f;
  }

  /* ---------- SEND LOGIK ---------- */
  bool sendMQTTnow = false;
  /* State change */
  if(state != lastState) sendMQTTnow = true;
  /* Heartbeat */
  if(millis() - lastMQTT > 10000) sendMQTTnow = true;
  float litersNow =
    (state == PRODUCTION) ? producedLitersSafe() : lastProducedLiters;

  /* ---------- Publish ---------- */
  if(sendMQTTnow) {
    mqttPublish(sName[state], tds, litersNow, currentFlowLpm, runtimeSec);
    lastState = state;
    lastMQTT  = millis();
  }
  
  updateLEDs(state);

  bool manualActive = inActive(PIN_SMANU);
  String status = buildStatusLine(tds);
  webSetStatus(status.c_str());   
  webLoop(tds, sName[state], litersNow, manualActive, runtimeSec, currentModeStr(), currentFlowLpm, currentFlowInLpm, ESP_VERSION);
}
