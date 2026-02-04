#include "settings.h"
#include "config_settings.h"

Settings settings;

#define CFG(key, def) (configDoc[key] | (def))

void settingsLoad()
{
  settings.pulsesPerLiterIn    = CFG("pulsesPerLiterIn", 100);
  settings.pulsesPerLiterOut   = CFG("pulsesPerLiterOut",100);

  settings.tdsLimit            = CFG("tdsLimit", 50);
  settings.maxFlushTimeSec     = CFG("maxFlushTimeSec", 120);
  settings.maxRuntimeSec       = CFG("maxRuntimeSec", 300);
  settings.maxProductionLiters = CFG("maxProductionLiters", 50);
  

  settings.autoStart = CFG("autoStart", false);

  settings.mqttHost = String(CFG("mqttHost","MyRasPi.local"));
  settings.mqttPort = CFG("mqttPort",1883);

  settings.mDNSName = String(CFG("mDNSName","osmose"));
  settings.apPassword = String(CFG("APPassWord","osmose"));

}

void settingsSave()
{
  configDoc["pulsesPerLiterIn"]  = settings.pulsesPerLiterIn;
  configDoc["pulsesPerLiterOut"] = settings.pulsesPerLiterOut;

  configDoc["tdsLimit"] = settings.tdsLimit;
  configDoc["maxFlushTimeSec"] = settings.maxFlushTimeSec;
  configDoc["maxRuntimeSec"] = settings.maxRuntimeSec;
  configDoc["maxProductionLiters"] = settings.maxProductionLiters;

  configDoc["autoStart"] = settings.autoStart;

  configDoc["mqttHost"] = settings.mqttHost;
  configDoc["mqttPort"] = settings.mqttPort;
  configDoc["mDNSName"] = settings.mDNSName;
  configDoc["APPassWord"] = settings.apPassword;


  configSave();
}
