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



/* ============================================================
   TABLE (persistent)
   ============================================================ */

struct Row{
  time_t startTs;
  time_t endTs;
  float  liters;
  char   reason[20];
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

  if(currentRow >= 0 && currentRow < MAX_ROWS)
    rows[currentRow].liters = produced;
}


/* ============================================================
   START / END PRODUCTION
   ============================================================ */

void historyStartProduction()
{
  currentRow = 0;

  uint8_t moveCount = min(rowCount, (uint8_t)(MAX_ROWS-1));
  if(moveCount > 0)
    memmove(&rows[1], &rows[0], sizeof(Row)*moveCount);

  rows[0] = {};
  rows[0].startTs = time(nullptr);

  if(rowCount < MAX_ROWS)
    rowCount++;

  saveTable();
}


void historyEndProduction(const char* reason)
{
  if(currentRow < 0) return;

  Row &r = rows[currentRow];

  r.endTs = time(nullptr);

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
  uint16_t samples = seconds / 30;
  if(samples > MAX_SAMPLES_24H) samples = MAX_SAMPLES_24H;

  JsonDocument doc;

  JsonArray t = doc["tds"].to<JsonArray>();
  JsonArray f = doc["flow"].to<JsonArray>();
  JsonArray p = doc["prod"].to<JsonArray>();

  int start = (idx - samples + MAX_SAMPLES_24H) % MAX_SAMPLES_24H;

  for(int i=0;i<samples;i++){
    int k=(start+i)%MAX_SAMPLES_24H;

    t.add(tdsBuf[k]);
    f.add(flowBuf[k]);
    p.add(prodBuf[k]);
  }

  String s;
  serializeJson(doc,s);
  return s;
}


/* ============================================================
   JSON – TABLE
   ============================================================ */

String historyGetTableJson()
{
  JsonDocument doc;

  JsonArray arr = doc.to<JsonArray>();

  for(int i=0;i<rowCount;i++){
    JsonObject o = arr.add<JsonObject>();

    o["start"]  = rows[i].startTs;
    o["end"]    = rows[i].endTs;
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

  // Datei leeren (persistent)
  File f = SPIFFS.open(FILE_NAME, "w");
  if(f) f.close();
}
