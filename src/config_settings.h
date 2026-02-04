#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

extern JsonDocument configDoc;

bool configLoad();
bool configSave();
bool configEnsureExists();
