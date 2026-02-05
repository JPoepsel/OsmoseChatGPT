#pragma once
#include <Arduino.h>

struct Settings
{
  float pulsesPerLiterIn;
  float pulsesPerLiterOut;

  float tdsLimit;
  float maxFlushTimeSec;
  float maxRuntimeAutoSec;
  float maxRuntimeManualSec;
  float maxProductionAutoLiters;
  float maxProductionManualLiters;

  bool  autoStart;
  String apPassword;
  String mqttHost;
  uint16_t mqttPort;
  String mDNSName;
  String wifiSSID;
  String wifiPassword;
};

extern Settings settings;

/* lädt aus configDoc → settings */
void settingsLoad();

/* schreibt settings → configDoc + speichert */
void settingsSave();
