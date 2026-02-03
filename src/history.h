#pragma once
#include <Arduino.h>

void historyInit();

void historyAddSample(float tds, float flow, float produced);

void historyStartProduction();
void historyEndProduction(const char* reason);

String historyGetSeriesJson(uint32_t seconds);
String historyGetTableJson();
uint8_t historyGetRowCount();
