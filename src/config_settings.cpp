#include "config_settings.h"
#include <SPIFFS.h>

/* ============================================================ */

JsonDocument configDoc;



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
  configDoc["maxFlushTimeSec"]     = 120;
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
