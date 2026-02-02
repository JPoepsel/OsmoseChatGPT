#include "web.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config_settings.h"


bool webStartRequest=false;
bool webStopRequest=false;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static uint32_t lastSend=0;

#define WEBDBG(...) Serial.printf(__VA_ARGS__)


/* ============================================================
   HELPER – disable browser cache
   ============================================================ */
static void addNoCache(AsyncWebServerResponse *r)
{
  r->addHeader("Cache-Control","no-cache, no-store, must-revalidate");
  r->addHeader("Pragma","no-cache");
  r->addHeader("Expires","0");
}


/* ============================================================
   Broadcast full status (WebSocket)
   ============================================================ */
static void wsBroadcast(float tds,
                        const char* stateName,
                        float litersNow,
                        float flowLpm,
                        float litersLeft)
{
  JsonDocument doc;

  doc["state"]=stateName;
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


/* ============================================================
   INIT
   ============================================================ */
void webInit()
{
  WEBDBG("[WEB] SPIFFS init\n");

  SPIFFS.begin(true);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);


  /* =========================
     STATIC FILES (NO CACHE)
  ========================= */
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


  /* =========================
     SETTINGS GET
  ========================= */
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){

    WEBDBG("[WEB] settings GET\n");

    String json;
 
    if(configDoc.isNull())
      json = "{}";
    else
      serializeJson(configDoc,json);

    auto r=request->beginResponse(200,"application/json",json);
    addNoCache(r);
    request->send(r);
  });


  /* =========================
     SETTINGS POST
  ========================= */
  server.on("/api/settings", HTTP_POST,
    [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t){

      WEBDBG("[WEB] settings POST\n");

      JsonDocument doc;
      deserializeJson(doc, data);

      configDoc.clear();
      configDoc.set(doc);

      configSave();

      request->send(200,"text/plain","OK");
    });


  server.begin();

  WEBDBG("[WEB] server ready\n");
}


/* ============================================================
   LOOP  (BLEIBT – dein Live-Status!)
   ============================================================ */
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

  float left = 50.0 - litersNow; // später CFG("maxProductionLiters")

  if(millis()-lastSend>300){
    wsBroadcast(tds,stateName,litersNow,flow,left);
    lastSend=millis();
  }
}
