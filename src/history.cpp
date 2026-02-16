#include "history.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

/* ============================================================
   CONFIG
   ============================================================ */

#define SAMPLE_INTERVAL_MS 30000
#define MAX_SAMPLES_24H (24*60*2)   // alle 30s = 2880
#define MAX_ROWS 100


/* ============================================================
   SERIES BUFFERS (RAM)
   ============================================================ */

static float tdsBuf[MAX_SAMPLES_24H];
static float flowBuf[MAX_SAMPLES_24H];
static float prodBuf[MAX_SAMPLES_24H];

static uint16_t idx = 0;
static uint32_t lastSampleMs = 0;
static float lastProducedLiters = 0.0f;   // ⭐ neu für Flow-Berechnung
static uint16_t sampleCount = 0;   // ⭐ Anzahl gültiger Samples



/* ============================================================
   TABLE (persistent)
   ============================================================ */

struct Row{
  time_t startTs;
  time_t endTs;
  float  liters;
  char   reason[20];
  char   mode[10];     
};

static Row rows[MAX_ROWS];
static uint8_t rowCount = 0;
static int currentRow = -1;

static const char* FILE_NAME="/history.bin";


/* ============================================================
   ⭐⭐⭐ UPDATE CALLBACK (NEU – PUSH STATT POLLING)
   ============================================================ */

static HistoryUpdateCallback updateCb = nullptr;

void historySetUpdateCallback(HistoryUpdateCallback cb)
{
  updateCb = cb;
}


/* ============================================================ */

uint8_t historyGetRowCount()
{
  return rowCount;
}


/* ============================================================
   SAVE / LOAD
   ============================================================ */

void saveTable()
{
  File f = SPIFFS.open(FILE_NAME,"w");
  if(!f) return;

  f.write((uint8_t*)&rowCount, sizeof(rowCount));
  f.write((uint8_t*)rows, sizeof(rows));

  f.close();

  // ⭐⭐⭐ SOFORT CLIENT INFORMIEREN ⭐⭐⭐
  if(updateCb) updateCb();
}


void loadTable()
{
  if(!SPIFFS.exists(FILE_NAME)) return;

  File f = SPIFFS.open(FILE_NAME,"r");
  if(!f) return;

  size_t n = 0;

  n += f.read((uint8_t*)&rowCount, sizeof(rowCount));
  n += f.read((uint8_t*)rows, sizeof(rows));

  if(n != sizeof(rowCount) + sizeof(rows)){
    memset(rows,0,sizeof(rows));
    rowCount = 0;
  }

  f.close();
}


/* ============================================================ */

void historyInit()
{
  loadTable();

  idx = 0;
  sampleCount = 0;
  lastProducedLiters = 0.0f;

  memset(tdsBuf,  0, sizeof(tdsBuf));
  memset(flowBuf, 0, sizeof(flowBuf));
  memset(prodBuf, 0, sizeof(prodBuf));
}


/* ============================================================
   SERIES SAMPLES
   ============================================================ */

void historyAddSample(float tds, float produced)
{
  if(millis() - lastSampleMs < SAMPLE_INTERVAL_MS) return;

  lastSampleMs = millis();

  /* ⭐ echter Flow in L/min */
  float delta = produced - lastProducedLiters;
  float flowLpm = delta * 2.0f;   // 30s sample → *2

  lastProducedLiters = produced;

  tdsBuf[idx]  = tds;
  flowBuf[idx] = flowLpm;   // ⭐ jetzt korrekt
  prodBuf[idx] = produced;

  idx = (idx+1) % MAX_SAMPLES_24H;
  if(sampleCount < MAX_SAMPLES_24H)
    sampleCount++;

}


/* ============================================================
   START / END PRODUCTION
   ============================================================ */

void historyStartProduction(const char* mode)
{
  currentRow = 0;

  uint8_t moveCount = min(rowCount, (uint8_t)(MAX_ROWS-1));
  if(moveCount > 0)
    memmove(&rows[1], &rows[0], sizeof(Row)*moveCount);

  rows[0] = {};
  rows[0].startTs = time(nullptr);

  strncpy(rows[0].mode, mode, sizeof(rows[0].mode)-1);
  rows[0].mode[sizeof(rows[0].mode)-1] = 0;

  if(rowCount < MAX_ROWS)
    rowCount++;

  saveTable();
}

void historyEndProduction(const char* reason, float finalLiters)
{
  if(currentRow < 0) return;

  Row &r = rows[currentRow];

  r.endTs  = time(nullptr);
  r.liters = finalLiters;   

  if(!reason || !reason[0])
    reason = "Stopped";

  strncpy(r.reason, reason, sizeof(r.reason)-1);
  r.reason[sizeof(r.reason)-1] = 0;

  currentRow = -1;
  saveTable();
}


/* ============================================================
   JSON – SERIES
   ============================================================ */

String historyGetSeriesJson(uint32_t seconds)
{
  uint16_t totalSamples = seconds / 30;
  if(totalSamples > MAX_SAMPLES_24H)
    totalSamples = MAX_SAMPLES_24H;

  
  StaticJsonDocument<32768> doc;


  JsonArray t = doc["tds"].to<JsonArray>();
  JsonArray f = doc["flow"].to<JsonArray>();
  JsonArray p = doc["prod"].to<JsonArray>();

  /* Anzahl leerer Samples am Anfang */
  uint16_t empty = 0;
  if(sampleCount < totalSamples)
    empty = totalSamples - sampleCount;

  /* 1️⃣ Leere Zeit mit null füllen */
  for(uint16_t i = 0; i < empty; i++) {
    t.add(nullptr);
    f.add(nullptr);
    p.add(nullptr);
  }

  /* 2️⃣ Echte Samples anhängen */
  uint16_t realSamples = min(sampleCount, totalSamples);
  int start = (int)idx - (int)realSamples;
  if(start < 0) start += MAX_SAMPLES_24H;

  for(uint16_t i = 0; i < realSamples; i++) {
    int k = (start + i) % MAX_SAMPLES_24H;
    t.add(tdsBuf[k]);
    f.add(flowBuf[k]);
    p.add(prodBuf[k]);
  }

  String s;
  serializeJson(doc, s);
  return s;
}


/* ============================================================
   JSON – TABLE
   ============================================================ */

String historyGetTableJson()
{
  StaticJsonDocument<8192> doc;    // <<< reicht hier

  JsonArray arr = doc.to<JsonArray>();

  for(int i=0;i<rowCount;i++){
    JsonObject o = arr.add<JsonObject>();

    o["mode"]   = rows[i].mode;
    o["start"]  = rows[i].startTs;
    o["end"]    = rows[i].endTs;

    uint32_t dur = 0;
    if(rows[i].startTs && rows[i].endTs)
      dur = rows[i].endTs - rows[i].startTs;

    o["duration"] = dur;

    o["liters"] = rows[i].liters;
    o["reason"] = rows[i].reason;

  }

  String s;
  serializeJson(doc,s);
  return s;
}

/* ============================================================
   CLEAR PRODUCTION TABLE ONLY
   ============================================================ */
void historyClearProduction()
{
  rowCount = 0;
  currentRow = -1;

  memset(rows, 0, sizeof(rows));

  idx = 0;
  sampleCount = 0;
  lastProducedLiters = 0.0f;

  memset(tdsBuf,  0, sizeof(tdsBuf));
  memset(flowBuf, 0, sizeof(flowBuf));
  memset(prodBuf, 0, sizeof(prodBuf));


  // Datei leeren (persistent)
  File f = SPIFFS.open(FILE_NAME, "w");
  if(f) f.close();
}
