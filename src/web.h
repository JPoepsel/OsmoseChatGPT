#pragma once
#include <Arduino.h>

extern bool webStartRequest;
extern bool webStopRequest;

void webInit();
void webLoop(float tds, const char* stateName, float litersNow);
