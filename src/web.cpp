#include "web.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>

#include "history.h"
#include "config_settings.h"
#include "settings.h"

extern String lastErrorMsg;

bool webStartRequest=false;
bool webStopRequest=false;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static uint32_t lastSend=0;


/* ============================================================
   ⭐ HISTORY CALLBACK (NEU)
   Wird von history.cpp gerufen wenn Tabelle sich ändert
   ============================================================ */

static void onHistoryUpdate()
{
  ws.textAll("{\"histUpdate\":1}");
}


/* ============================================================ */
static void addNoCache(AsyncWebServerResponse *r)
{
  r->addHeader("Cache-Control","no-cache, no-store, must-revalidate");
  r->addHeader("Pragma","no-cache");
  r->addHeader("Expires","0");
}


/* ============================================================ */
static void wsBroadcast(float tds,
                        const char* stateName,
                        float litersNow,
                        float flowLpm,
                        float litersLeft,
                        float runtimeLeft)
{
  JsonDocument doc;

  doc["state"]=stateName;
  doc["error"]=lastErrorMsg;
  doc["tds"]=tds;
  doc["liters"]=litersNow;
  doc["flow"]=flowLpm;
  doc["left"]=litersLeft;
  doc["timeLeft"] = runtimeLeft;

  String s;
  serializeJson(doc,s);

  ws.textAll(s);
}


/* ============================================================ */
static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*,
                      AwsEventType type, void*, uint8_t* data, size_t len)
{
  if(type!=WS_EVT_DATA) return;

  String m=String((char*)data).substring(0,len);

  if(m=="start") webStartRequest=true;
  if(m=="stop")  webStopRequest=true;
}


/* ============================================================ */
void webInit()
{
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  historySetUpdateCallback(onHistoryUpdate);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    auto r=request->beginResponse(SPIFFS,"/index.html","text/html");
    addNoCache(r);
    request->send(r);
  });

  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    auto r=request->beginResponse(SPIFFS,"/app.js","application/javascript");
    addNoCache(r);
    request->send(r);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    auto r=request->beginResponse(SPIFFS,"/style.css","text/css");
    addNoCache(r);
    request->send(r);
  });


  /* SETTINGS GET */
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){

    String json;
    serializeJson(configDoc,json);

    auto r=request->beginResponse(200,"application/json",json);
    addNoCache(r);
    request->send(r);
  });


 /* ================= WIFI SCAN (async, WDT-safe) ================= */
server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req){

  int status = WiFi.scanComplete();
  Serial.printf("[SCAN] status=%d\n", status);

  // läuft noch
  if(status == WIFI_SCAN_RUNNING){
    req->send(202,"text/plain","running");
    return;
  }

  // erster Start oder fehlgeschlagen -> async starten
  if(status == WIFI_SCAN_FAILED || status < 0){
    Serial.println("[SCAN] start async");
    WiFi.scanNetworks(true);   // ⭐ async
    req->send(202,"text/plain","starting");
    return;
  }

  // fertig
  int n = status;
  Serial.printf("[SCAN] finished: %d\n", n);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  struct Net { String s; int r; };
  std::vector<Net> nets;

  for(int i=0;i<n;i++){
    String ssid = WiFi.SSID(i);
    int rssi    = WiFi.RSSI(i);

    if(!ssid.length()) continue;

    bool found=false;
    for(size_t k=0;k<nets.size();k++){
      if(nets[k].s == ssid){
        if(rssi > nets[k].r) nets[k].r = rssi;
        found=true;
        break;
      }
    }
    if(!found) nets.push_back(Net{ssid,rssi});
  }

  std::sort(nets.begin(), nets.end(),
    [](const Net& a,const Net& b){ return a.r>b.r; });

  for(size_t i=0;i<nets.size();i++){
    JsonObject o = arr.add<JsonObject>();
    o["ssid"]=nets[i].s;
    o["rssi"]=nets[i].r;
  }

  WiFi.scanDelete();

  String s;
  serializeJson(doc,s);
  req->send(200,"application/json",s);
});



  /* SETTINGS POST */
  server.on("/api/settings", HTTP_POST,
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t){

      JsonDocument doc;
      if(deserializeJson(doc,data))
        return request->send(400,"text/plain","Bad JSON");

     for (JsonPair kv : doc.as<JsonObject>())
       configDoc[kv.key()] = kv.value();

      configSave();
      settingsLoad();

      request->send(200,"text/plain","OK");
    });


  server.on("/api/history/series", HTTP_GET, [](AsyncWebServerRequest *req){
    uint32_t range = 3600;
    if(req->hasParam("range"))
      range = req->getParam("range")->value().toInt();

    req->send(200, "application/json", historyGetSeriesJson(range));
  });

/* REBOOT */
server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req){
  req->send(200,"text/plain","rebooting");
  delay(200);
  ESP.restart();
});

  server.on("/api/history/table", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "application/json", historyGetTableJson());
  });

  server.begin();
}


/* ============================================================ */
void webLoop(float tds, const char* stateName, float litersNow, bool isManualMode, uint32_t runtimeSec)
{
  static uint32_t lastCnt=0;
  static uint32_t lastT=millis();

  float flow=0;

  if(millis()-lastT>1000){
    flow=(litersNow-lastCnt)*60.0;
    lastCnt=litersNow;
    lastT=millis();
  }

  float limit = isManualMode ?
  settings.maxProductionManualLiters :
  settings.maxProductionAutoLiters;

  float left = (limit > 0) ? (limit - litersNow) : 0;
  float runtimeLeft = 0;

  float runLimit = isManualMode ?
    settings.maxRuntimeManualSec :
    settings.maxRuntimeAutoSec;

  if(runLimit > 0)  {
    float elapsed = runtimeSec;
    runtimeLeft = runLimit - elapsed;
    if(runtimeLeft < 0) runtimeLeft = 0;
  }

  if(millis()-lastSend>300){
    wsBroadcast(tds,stateName,litersNow,flow,left,runtimeLeft);
    lastSend=millis();
  }
}
