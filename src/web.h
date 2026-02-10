#pragma once
#include <Arduino.h>

extern bool webStartRequest;
extern bool webStopRequest;

void webInit();
void webLoop(float tds, const char* state, float liters, bool manual, uint32_t runtime, const char* mode);
void webNotifyHistoryUpdate();



