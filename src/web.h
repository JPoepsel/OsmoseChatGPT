#pragma once
#include <Arduino.h>

extern bool webStartRequest;
extern bool webStopRequest;

void webInit();
void webSetStatus(const char* s);
void webLoop(float tds, const char* state, float liters, bool manual, uint32_t runtime, 
              const char* mode, float flowLpm, const char* espVersion);
void webNotifyHistoryUpdate();



