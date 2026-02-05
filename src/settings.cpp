#include "config_defaults.h"
#include "settings.h"
#include "config_settings.h"

Settings settings;

#define CFG(key, def) (configDoc[key] | (def))

void settingsLoad()
{
  // ===== Flow calibration =====
  settings.pulsesPerLiterIn  = CFG("pulsesPerLiterIn",  DEF_PULSES_PER_LITER_IN);
  settings.pulsesPerLiterOut = CFG("pulsesPerLiterOut", DEF_PULSES_PER_LITER_OUT);


  // ===== Process =====
  settings.tdsLimit        = CFG("tdsLimit",        DEF_TDS_LIMIT);
  settings.maxFlushTimeSec = CFG("maxFlushTimeSec", DEF_MAX_FLUSH_TIME_SEC);

  settings.maxRuntimeAutoSec     = CFG("maxRuntimeAutoSec",     DEF_MAX_RUNTIME_AUTO_SEC);
  settings.maxRuntimeManualSec   = CFG("maxRuntimeManualSec",   DEF_MAX_RUNTIME_MANUAL_SEC);

  settings.maxProductionAutoLiters   = CFG("maxProductionAutoLiters",   DEF_MAX_PROD_AUTO_L);
  settings.maxProductionManualLiters = CFG("maxProductionManualLiters", DEF_MAX_PROD_MANUAL_L);


  // ===== System =====
  settings.autoStart = CFG("autoStart", DEF_AUTOSTART);

  settings.mqttHost   = String(CFG("mqttHost", DEF_MQTT_HOST));
  settings.mqttPort   = CFG("mqttPort", DEF_MQTT_PORT);
  settings.mDNSName   = String(CFG("mDNSName", DEF_MDNS_NAME));
  settings.apPassword = String(CFG("APPassWord", DEF_AP_PASSWORD));
  settings.wifiSSID     = String(CFG("wifiSSID", DEF_WIFI_SSID));
  settings.wifiPassword = String(CFG("wifiPassword", DEF_WIFI_PASSWORD));
}

void settingsSave()
{
  configDoc["pulsesPerLiterIn"]  = settings.pulsesPerLiterIn;
  configDoc["pulsesPerLiterOut"] = settings.pulsesPerLiterOut;

  configDoc["tdsLimit"] = settings.tdsLimit;
  configDoc["maxFlushTimeSec"] = settings.maxFlushTimeSec;
  configDoc["maxRuntimeAutoSec"]     = settings.maxRuntimeAutoSec;
  configDoc["maxRuntimeManualSec"]   = settings.maxRuntimeManualSec;

  configDoc["maxProductionAutoLiters"]   = settings.maxProductionAutoLiters;
  configDoc["maxProductionManualLiters"] = settings.maxProductionManualLiters;

  configDoc["autoStart"] = settings.autoStart;

  configDoc["mqttHost"] = settings.mqttHost;
  configDoc["mqttPort"] = settings.mqttPort;
  configDoc["mDNSName"] = settings.mDNSName;
  configDoc["APPassWord"] = settings.apPassword;
  configDoc["wifiSSID"]     = settings.wifiSSID;
  configDoc["wifiPassword"] = settings.wifiPassword;

  configSave();
}
