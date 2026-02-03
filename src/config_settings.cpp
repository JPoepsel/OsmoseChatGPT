#include "config_settings.h"
#include <SPIFFS.h>

/* ============================================================ */

JsonDocument configDoc;

float producedLiters = 0.0f;

static bool productionLockedManual = false;
static bool productionLockedAuto   = false;
static bool lastLowSwitch = false;


/* ============================================================
   CONFIG LOAD/SAVE
   ============================================================ */

bool configLoad()
{
  if(!SPIFFS.exists("/config.json")){
    Serial.println("[CFG] no config.json -> defaults");
    return false;
  }

  File f = SPIFFS.open("/config.json","r");
  if(!f) return false;

  auto err = deserializeJson(configDoc, f);
  f.close();

  if(err){
    Serial.println("[CFG] load failed");
    return false;
  }

  Serial.println("[CFG] loaded");
  return true;
}


bool configSave()
{
  File f = SPIFFS.open("/config.json","w");
  if(!f) return false;

  serializeJsonPretty(configDoc, f);
  f.close();

  Serial.println("[CFG] saved");
  return true;
}


/* ============================================================
   PRODUCTION LIMIT LOGIC
   ============================================================ */

void prodAdd(float d)
{
  producedLiters += d;
  Serial.printf("[PROD] +%.3fL total=%.3fL\n", d, producedLiters);
}


bool prodCheckLimit(bool isManualMode)
{
  float maxProd = CFG("maxProductionLiters", 0.0f);

  if(maxProd <= 0) return false;

  if(producedLiters >= maxProd)
  {
    Serial.println("[PROD] limit reached");

    if(isManualMode){
      productionLockedManual = true;
      Serial.println("[PROD] locked (manual)");
    }
    else{
      productionLockedAuto = true;
      Serial.println("[PROD] locked (auto)");
    }

    return true;
  }

  return false;
}


void prodHandleResets(bool lowSwitch, bool switchOffToOn)
{
  if(productionLockedAuto && lastLowSwitch && !lowSwitch){
    producedLiters = 0;
    productionLockedAuto = false;
    Serial.println("[PROD] auto reset");
  }

  if(productionLockedManual && switchOffToOn){
    producedLiters = 0;
    productionLockedManual = false;
    Serial.println("[PROD] manual reset");
  }

  lastLowSwitch = lowSwitch;
}

/* ============================================================
   DEFAULT CONFIG CREATION (ADD ONLY)
   ============================================================ */

static void configWriteDefaults()
{
  Serial.println("[CFG] creating default config.json");

  configDoc.clear();

  // ===== Flow calibration
  configDoc["pulsesPerLiterIn"]    = 100;
  configDoc["pulsesPerLiterOut"]   = 100;

  // ===== Process
  configDoc["tdsLimit"]            = 50;
  configDoc["flushTimeSec"]        = 12;
  configDoc["maxRuntimeSec"]       = 300;
  configDoc["maxProductionLiters"] = 50.0;

  // ===== System
  configDoc["autoStart"]           = false;
  configDoc["mqttHost"]            = "192.168.68.57";
  configDoc["mqttPort"]            = 1883;
  configDoc["mDNSName"]            = "osmose";

  configSave();
}


bool configEnsureExists()
{
  
  if(!SPIFFS.exists("/config.json")){
    configWriteDefaults();
  }

  return true;
}
