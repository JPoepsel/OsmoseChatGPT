#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/* ============================================================
   GLOBAL CONFIG (JSON)
   ArduinoJson v7 style (JsonDocument, not deprecated Dynamic*)
   ============================================================ */

extern JsonDocument configDoc;

bool configLoad();
bool configSave();

/* fallback helper
   usage:
   float v = CFG("tdsLimit", 500);
*/
#define CFG(key, def) (configDoc[key] | (def))


/* ============================================================
   PRODUCTION LIMIT SYSTEM
   ============================================================ */

extern float producedLiters;

/* add produced liters each cycle */
void prodAdd(float deltaLiters);

/* check against maxProductionLiters */
void prodCheckLimit(bool isManualMode);

/* handle reset logic
   lowSwitch        = current LOW float state
   switchOffToOn    = manual/auto switch rising edge
*/
void prodHandleResets(bool lowSwitch, bool switchOffToOn);
