#pragma once
#include <Arduino.h>

/* ============================================================
   HISTORY UPDATE CALLBACK ⭐ NEU
   ============================================================ */

typedef void (*HistoryUpdateCallback)();

void historySetUpdateCallback(HistoryUpdateCallback cb);

/* ============================================================ */

void historyInit();

enum HistorySeries {
  HIST_2S,
  HIST_30S,
  HIST_600S
};

void historyAddSample2s(float tds,
                        float produced,
                        float flowOutLpm,
                        float flowInLpm);
String historyGetSeriesJson(HistorySeries series);



void historyStartProduction(const char* mode);
void historyEndProduction(const char* reason, float finalLiters);
void historyClearProduction();

String historyGetTableJson();
uint8_t historyGetRowCount();
