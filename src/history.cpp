#include "history.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

/* ============================================================
   SERIES CONFIG
   ============================================================ */

#define HIST_2S_COUNT    150     // ~5 Minuten
#define HIST_30S_COUNT   150     // ~75 Minuten
#define HIST_600S_COUNT  150     // ~25 Stunden

/* ============================================================
   SERIES BUFFERS (RAM, FIXED SIZE, ZERO-FILLED)
   ============================================================ */

static float tds2s[HIST_2S_COUNT];
static float flow2s[HIST_2S_COUNT];
static float prod2s[HIST_2S_COUNT];

static float tds30s[HIST_30S_COUNT];
static float flow30s[HIST_30S_COUNT];
static float prod30s[HIST_30S_COUNT];

static float tds600s[HIST_600S_COUNT];
static float flow600s[HIST_600S_COUNT];
static float prod600s[HIST_600S_COUNT];

static uint16_t idx2s   = 0;
static uint16_t idx30s  = 0;
static uint16_t idx600s = 0;

static uint32_t last2sMs = 0;

/* aggregation helpers */
static float accTds30   = 0;
static float accFlow30  = 0;
static uint8_t accCnt30 = 0;

static float accTds600    = 0;
static float accFlow600   = 0;
static uint16_t accCnt600 = 0;

/* ============================================================
   TABLE (persistent, UNCHANGED)
   ============================================================ */

#define MAX_ROWS 100
static const char* FILE_NAME = "/history.bin";

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

/* ============================================================
   UPDATE CALLBACK
   ============================================================ */

static HistoryUpdateCallback updateCb = nullptr;

void historySetUpdateCallback(HistoryUpdateCallback cb)
{
  updateCb = cb;
}

/* ============================================================
   TABLE SAVE / LOAD
   ============================================================ */

static void saveTable()
{
  File f = SPIFFS.open(FILE_NAME, "w");
  if(!f) return;

  f.write((uint8_t*)&rowCount, sizeof(rowCount));
  f.write((uint8_t*)rows, sizeof(rows));
  f.close();

  if(updateCb) updateCb();
}

static void loadTable()
{
  if(!SPIFFS.exists(FILE_NAME)) return;

  File f = SPIFFS.open(FILE_NAME, "r");
  if(!f) return;

  size_t n = 0;
  n += f.read((uint8_t*)&rowCount, sizeof(rowCount));
  n += f.read((uint8_t*)rows, sizeof(rows));

  if(n != sizeof(rowCount) + sizeof(rows)) {
    memset(rows, 0, sizeof(rows));
    rowCount = 0;
  }

  f.close();
}

/* ============================================================
   INIT
   ============================================================ */

void historyInit()
{
  loadTable();

  memset(tds2s,   0, sizeof(tds2s));
  memset(flow2s,  0, sizeof(flow2s));
  memset(prod2s,  0, sizeof(prod2s));

  memset(tds30s,  0, sizeof(tds30s));
  memset(flow30s, 0, sizeof(flow30s));
  memset(prod30s, 0, sizeof(prod30s));

  memset(tds600s,  0, sizeof(tds600s));
  memset(flow600s, 0, sizeof(flow600s));
  memset(prod600s, 0, sizeof(prod600s));

  idx2s = idx30s = idx600s = 0;
  accCnt30 = accCnt600 = 0;
}

/* ============================================================
   2s BASE SAMPLE + AGGREGATION
   ============================================================ */

void historyAddSample2s(float tds,
                        float produced,
                        float flowOutLpm,
                        float flowInLpm)
{
  uint32_t now = millis();
  if(now - last2sMs < 2000) return;
  last2sMs = now;

  /* --- 2s --- */
  tds2s[idx2s]  = tds;
  flow2s[idx2s] = flowOutLpm;
  prod2s[idx2s] = produced;
  idx2s = (idx2s + 1) % HIST_2S_COUNT;

  /* --- 30s aggregation (15 × 2s) --- */
  accTds30  += tds;
  accFlow30 += flowOutLpm;
  accCnt30++;

  if(accCnt30 >= 15) {
    tds30s[idx30s]  = accTds30 / accCnt30;
    flow30s[idx30s] = accFlow30 / accCnt30;
    prod30s[idx30s] = produced;

    idx30s = (idx30s + 1) % HIST_30S_COUNT;
    accTds30 = accFlow30 = 0;
    accCnt30 = 0;
  }

  /* --- 600s aggregation (300 × 2s) --- */
  accTds600  += tds;
  accFlow600 += flowOutLpm;
  accCnt600++;

  if(accCnt600 >= 300) {
    tds600s[idx600s]  = accTds600 / accCnt600;
    flow600s[idx600s] = accFlow600 / accCnt600;
    prod600s[idx600s] = produced;

    idx600s = (idx600s + 1) % HIST_600S_COUNT;
    accTds600 = accFlow600 = 0;
    accCnt600 = 0;
  }
}


/* ============================================================
   SERIES JSON
   ============================================================ */

String historyGetSeriesJson(HistorySeries s)
{
  StaticJsonDocument<16384> doc;
  JsonArray t = doc["tds"].to<JsonArray>();
  JsonArray f = doc["flow"].to<JsonArray>();
  JsonArray p = doc["prod"].to<JsonArray>();

  float *tdsArr, *flowArr, *prodArr;
  uint16_t count, idx;

  if(s == HIST_2S) {
    tdsArr = tds2s; flowArr = flow2s; prodArr = prod2s;
    count = HIST_2S_COUNT; idx = idx2s;
  }
  else if(s == HIST_30S) {
    tdsArr = tds30s; flowArr = flow30s; prodArr = prod30s;
    count = HIST_30S_COUNT; idx = idx30s;
  }
  else {
    tdsArr = tds600s; flowArr = flow600s; prodArr = prod600s;
    count = HIST_600S_COUNT; idx = idx600s;
  }

  for(uint16_t i = 0; i < count; i++) {
    uint16_t k = (idx + i) % count;
    t.add(tdsArr[k]);
    f.add(flowArr[k]);
    p.add(prodArr[k]);
  }

  String out;
  serializeJson(doc, out);
  return out;
}

/* ============================================================
   PRODUCTION TABLE (UNCHANGED BEHAVIOR)
   ============================================================ */

uint8_t historyGetRowCount()
{
  return rowCount;
}

void historyStartProduction(const char* mode)
{
  currentRow = 0;

  uint8_t moveCount = min(rowCount, (uint8_t)(MAX_ROWS - 1));
  if(moveCount > 0)
    memmove(&rows[1], &rows[0], sizeof(Row) * moveCount);

  rows[0] = {};
  rows[0].startTs = time(nullptr);

  strncpy(rows[0].mode, mode, sizeof(rows[0].mode) - 1);
  rows[0].mode[sizeof(rows[0].mode) - 1] = 0;

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

  strncpy(r.reason, reason, sizeof(r.reason) - 1);
  r.reason[sizeof(r.reason) - 1] = 0;

  currentRow = -1;
  saveTable();
}

String historyGetTableJson()
{
  StaticJsonDocument<8192> doc;
  JsonArray arr = doc.to<JsonArray>();

  for(int i = 0; i < rowCount; i++) {
    JsonObject o = arr.add<JsonObject>();

    o["mode"]   = rows[i].mode;
    o["start"]  = rows[i].startTs;
    o["end"]    = rows[i].endTs;

    uint32_t dur = 0;
    if(rows[i].startTs && rows[i].endTs)
      dur = rows[i].endTs - rows[i].startTs;

    o["duration"] = dur;
    o["liters"]   = rows[i].liters;
    o["reason"]   = rows[i].reason;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void historyClearProduction()
{
  rowCount = 0;
  currentRow = -1;
  memset(rows, 0, sizeof(rows));

  File f = SPIFFS.open(FILE_NAME, "w");
  if(f) f.close();

  if(updateCb) updateCb();
}
