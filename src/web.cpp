#include "web.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

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
                        float litersLeft)
{
  JsonDocument doc;

  doc["state"]=stateName;
  doc["error"]=lastErrorMsg;
  doc["tds"]=tds;
  doc["liters"]=litersNow;
  doc["flow"]=flowLpm;
  doc["left"]=litersLeft;

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

  /* ⭐⭐⭐ HIER REGISTRIEREN ⭐⭐⭐ */
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


  /* SETTINGS POST */
  server.on("/api/settings", HTTP_POST,
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t){

      JsonDocument doc;
      deserializeJson(doc, data);

      configDoc.clear();
      configDoc.set(doc);
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

  server.on("/api/history/table", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "application/json", historyGetTableJson());
  });


  server.begin();
}


/* ============================================================ */
void webLoop(float tds, const char* stateName, float litersNow)
{
  static uint32_t lastCnt=0;
  static uint32_t lastT=millis();

  float flow=0;

  if(millis()-lastT>1000){
    flow=(litersNow-lastCnt)*60.0;
    lastCnt=litersNow;
    lastT=millis();
  }

  float left = settings.maxProductionLiters - litersNow;

  if(millis()-lastSend>300){
    wsBroadcast(tds,stateName,litersNow,flow,left);
    lastSend=millis();
  }
}
