#include "config_defaults.h"
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

  configDoc["pulsesPerLiterIn"]    = DEF_PULSES_PER_LITER_IN;
  configDoc["pulsesPerLiterOut"]   = DEF_PULSES_PER_LITER_OUT;

  configDoc["tdsLimit"]            = DEF_TDS_LIMIT;
  configDoc["maxFlushTimeSec"]     = DEF_MAX_FLUSH_TIME_SEC;

  configDoc["maxRuntimeAutoSec"]   = DEF_MAX_RUNTIME_AUTO_SEC;
  configDoc["maxRuntimeManualSec"] = DEF_MAX_RUNTIME_MANUAL_SEC;

  configDoc["maxProductionAutoLiters"]   = DEF_MAX_PROD_AUTO_L;
  configDoc["maxProductionManualLiters"] = DEF_MAX_PROD_MANUAL_L;

  configDoc["autoStart"] = DEF_AUTOSTART;
  configDoc["mqttPort"]  = DEF_MQTT_PORT;
  configDoc["mDNSName"]  = DEF_MDNS_NAME;
  configDoc["APPassWord"] = DEF_AP_PASSWORD;
  configDoc["mqttHost"]  = DEF_MQTT_HOST;
  configDoc["wifiSSID"] = DEF_WIFI_SSID;
  configDoc["wifiPassword"] = DEF_WIFI_PASSWORD;


  configSave();
}


bool configEnsureExists()
{
  
  if(!SPIFFS.exists("/config.json")){
    configWriteDefaults();
  }

  return true;
}
