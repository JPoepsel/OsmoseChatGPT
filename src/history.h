#pragma once
#include <Arduino.h>

/* ============================================================
   HISTORY UPDATE CALLBACK ⭐ NEU
   ============================================================ */

typedef void (*HistoryUpdateCallback)();

void historySetUpdateCallback(HistoryUpdateCallback cb);

/* ============================================================ */

void historyInit();

void historyAddSample(float tds, float produced);

void historyStartProduction();
void historyEndProduction(const char* reason);

String historyGetSeriesJson(uint32_t seconds);
String historyGetTableJson();
uint8_t historyGetRowCount();
