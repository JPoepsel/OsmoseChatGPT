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

