#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/* ============================================================
   GLOBAL CONFIG (ArduinoJson v7)
   ============================================================ */

extern JsonDocument configDoc;

bool configLoad();
bool configSave();

/* helper with default */
#define CFG(key, def) (configDoc[key] | (def))


/* ============================================================
   PRODUCTION LIMIT SYSTEM
   ============================================================ */

extern float producedLiters;

void prodAdd(float deltaLiters);

/* returns true -> limit reached */
bool prodCheckLimit(bool isManualMode);

void prodHandleResets(bool lowSwitch, bool switchOffToOn);

bool configEnsureExists();   // create default config if missing
