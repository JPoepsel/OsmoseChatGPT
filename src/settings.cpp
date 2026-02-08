#include "settings.h"
#include "config_settings.h"

Settings settings;


/* ============================================================
   GENERIC LOADERS
   ============================================================ */

template<typename T>
static void loadIfExists(const char* key, T& target)
{
  if(!configDoc[key].isNull())
    target = configDoc[key].as<T>();
}

/* overload für String */
static void loadIfExists(const char* key, String& target)
{
  if(!configDoc[key].isNull())
    target = String((const char*)configDoc[key]);
}


/* ============================================================
   LOAD
   ============================================================ */

void settingsLoad()
{
  /* Flow */
  loadIfExists("pulsesPerLiterIn",  settings.pulsesPerLiterIn);
  loadIfExists("pulsesPerLiterOut", settings.pulsesPerLiterOut);

  /* Process */
  loadIfExists("tdsLimit",        settings.tdsLimit);
  loadIfExists("maxFlushTimeSec", settings.maxFlushTimeSec);
  loadIfExists("tdsMaxAllowed", settings.tdsMaxAllowed);


  loadIfExists("maxRuntimeAutoSec",   settings.maxRuntimeAutoSec);
  loadIfExists("maxRuntimeManualSec", settings.maxRuntimeManualSec);

  loadIfExists("maxProductionAutoLiters",   settings.maxProductionAutoLiters);
  loadIfExists("maxProductionManualLiters", settings.maxProductionManualLiters);

  loadIfExists("prepareTimeSec",    settings.prepareTimeSec);
  loadIfExists("autoFlushEnabled",  settings.autoFlushEnabled);
  loadIfExists("postFlushEnabled",  settings.postFlushEnabled);
  loadIfExists("postFlushTimeSec",  settings.postFlushTimeSec);

  loadIfExists("serviceFlushEnabled",     settings.serviceFlushEnabled);
  loadIfExists("serviceFlushIntervalSec", settings.serviceFlushIntervalSec);
  loadIfExists("serviceFlushTimeSec",     settings.serviceFlushTimeSec);

  /* System */
  
  loadIfExists("mqttHost", settings.mqttHost);
  loadIfExists("mqttPort", settings.mqttPort);
  loadIfExists("mDNSName", settings.mDNSName);
  loadIfExists("APPassWord", settings.apPassword);

  loadIfExists("wifiSSID",     settings.wifiSSID);
  loadIfExists("wifiPassword", settings.wifiPassword);
}


/* ============================================================
   SAVE (write everything back)
   ============================================================ */

void settingsSave()
{
  /* Flow */
  configDoc["pulsesPerLiterIn"]  = settings.pulsesPerLiterIn;
  configDoc["pulsesPerLiterOut"] = settings.pulsesPerLiterOut;

  /* Process */
  configDoc["tdsLimit"]        = settings.tdsLimit;
  configDoc["maxFlushTimeSec"] = settings.maxFlushTimeSec;
  configDoc["tdsMaxAllowed"] = settings.tdsMaxAllowed;

  configDoc["maxRuntimeAutoSec"]   = settings.maxRuntimeAutoSec;
  configDoc["maxRuntimeManualSec"] = settings.maxRuntimeManualSec;

  configDoc["maxProductionAutoLiters"]   = settings.maxProductionAutoLiters;
  configDoc["maxProductionManualLiters"] = settings.maxProductionManualLiters;

  configDoc["prepareTimeSec"]   = settings.prepareTimeSec;
  configDoc["autoFlushEnabled"] = settings.autoFlushEnabled;
  configDoc["postFlushEnabled"] = settings.postFlushEnabled;
  configDoc["postFlushTimeSec"] = settings.postFlushTimeSec;

  configDoc["serviceFlushEnabled"]     = settings.serviceFlushEnabled;
  configDoc["serviceFlushIntervalSec"] = settings.serviceFlushIntervalSec;
  configDoc["serviceFlushTimeSec"]     = settings.serviceFlushTimeSec;

  /* System */
  configDoc["mqttHost"] = settings.mqttHost;
  configDoc["mqttPort"] = settings.mqttPort;
  configDoc["mDNSName"] = settings.mDNSName;
  configDoc["APPassWord"] = settings.apPassword;

  configDoc["wifiSSID"]     = settings.wifiSSID;
  configDoc["wifiPassword"] = settings.wifiPassword;

  configSave();
}
