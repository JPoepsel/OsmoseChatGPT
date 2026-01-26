#include "web.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static uint32_t lastSend=0;

#define WEBDBG(...) Serial.printf(__VA_ARGS__)

// ===== ADD: hooks into main.cpp =====
extern void webRequestStart();
extern void webRequestStop();
// ====================================


// ============================================================
// Broadcast full status
// ============================================================
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

// ============================================================
static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*,
                      AwsEventType type, void*, uint8_t* data, size_t len)
{
  if(type!=WS_EVT_DATA) return;

  String m=String((char*)data).substring(0,len);

  // ===== CHANGED =====
  if(m=="start") webRequestStart();
  if(m=="stop")  webRequestStop();
  // ===================
}

// ============================================================
void webInit()
{
  WEBDBG("[WEB] SPIFFS init\n");

  SPIFFS.begin(true);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.begin();

  WEBDBG("[WEB] server ready\n");
}

// ============================================================
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

  float left = 50.0 - litersNow;

  if(millis()-lastSend>300){
    wsBroadcast(tds,stateName,litersNow,flow,left);
    lastSend=millis();
  }
}
