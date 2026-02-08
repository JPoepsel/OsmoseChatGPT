#include "web.h"

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <Update.h>


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

void webNotifyHistoryUpdate()
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

const char* page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="refresh" content="25;url=/" />
<style>
body{
  background:#121212;
  color:#eee;
  font-family:system-ui;
  display:flex;
  align-items:center;
  justify-content:center;
  height:100vh;
}
.card{
  background:#1c1c1c;
  padding:30px;
  border-radius:14px;
  text-align:center;
}
</style>
</head>
<body>
<div class="card">
<h2>Update erfolgreich ✓</h2>
<p>Gerät startet neu…</p>
<p>Reload in <span id="c">25</span>s</p>
</div>

<script>
let s=25;
setInterval(()=>{
  document.getElementById("c").innerText=--s;
},1000);
</script>
</body>
</html>
)rawliteral";

/* ============================================================ */
void webInit()
{
  /* alle statischen Dateien */
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  historySetUpdateCallback(onHistoryUpdate);

  /* SETTINGS GET */
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){

  JsonDocument doc;

  doc["pulsesPerLiterIn"]  = settings.pulsesPerLiterIn;
  doc["pulsesPerLiterOut"] = settings.pulsesPerLiterOut;

  doc["tdsLimit"]        = settings.tdsLimit;
  doc["maxFlushTimeSec"] = settings.maxFlushTimeSec;
  doc["tdsMaxAllowed"]   = settings.tdsMaxAllowed; 

  doc["maxRuntimeAutoSec"]   = settings.maxRuntimeAutoSec;
  doc["maxRuntimeManualSec"] = settings.maxRuntimeManualSec;

  doc["maxProductionAutoLiters"]   = settings.maxProductionAutoLiters;
  doc["maxProductionManualLiters"] = settings.maxProductionManualLiters;

  doc["prepareTimeSec"]   = settings.prepareTimeSec;
  doc["autoFlushEnabled"] = settings.autoFlushEnabled;
  doc["postFlushEnabled"] = settings.postFlushEnabled;
  doc["postFlushTimeSec"] = settings.postFlushTimeSec;

  doc["serviceFlushEnabled"]     = settings.serviceFlushEnabled;
  doc["serviceFlushIntervalSec"] = settings.serviceFlushIntervalSec;
  doc["serviceFlushTimeSec"]     = settings.serviceFlushTimeSec;

  doc["mqttHost"] = settings.mqttHost;
  doc["mqttPort"] = settings.mqttPort;
  doc["mDNSName"] = settings.mDNSName;
  doc["APPassWord"] = settings.apPassword;
  doc["wifiSSID"] = settings.wifiSSID;
  doc["wifiPassword"] = settings.wifiPassword;

  String json;
  serializeJson(doc,json);

  auto r=request->beginResponse(200,"application/json",json);
  addNoCache(r);
  request->send(r);
});



/* ===== CLEAR PRODUCTION TABLE ONLY ===== */
server.on("/api/history/clearProd", HTTP_POST, [](AsyncWebServerRequest *req){

  historyClearProduction();   // ⭐ neue Funktion
  req->send(200,"text/plain","OK");

  onHistoryUpdate();          // sofort WS refresh
});

/* ================= OTA UPDATE ================= */

server.on("/update", HTTP_POST,
[](AsyncWebServerRequest *request){

    request->send(200,"text/html",page);

    // ⭐ reboot async verzögert
    xTaskCreate([](void*){
        delay(800);
        ESP.restart();
    }, "reboot", 2048, NULL, 1, NULL);

},
[](AsyncWebServerRequest *request, String filename, size_t index,
   uint8_t *data, size_t len, bool final)
{
    bool spiffs = request->hasParam("spiffs");

    if(!index)
    {
        if(spiffs)
            Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS);
        else
            Update.begin(UPDATE_SIZE_UNKNOWN);
    }

    Update.write(data, len);

    if(final)
        Update.end(true);
});

server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/update.html", "text/html");
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

      settingsLoad();   // config -> settings
      settingsSave();   // settings -> komplette config neu schreiben

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

  server.on("/ls", HTTP_GET, [](AsyncWebServerRequest *req){
    File root = SPIFFS.open("/");
    String out;
    File f = root.openNextFile();
    while(f){
      out += f.name();
      out += "\n";
      f = root.openNextFile();
    }
    req->send(200,"text/plain",out);
  });

  server.on("/api/history/table", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "application/json", historyGetTableJson());
  });

  /* ⭐⭐⭐ Captive Portal Fallback */
  server.onNotFound([](AsyncWebServerRequest *request){
    request->redirect("/");
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
