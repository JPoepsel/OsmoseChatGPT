#pragma once
#include <Arduino.h>

struct Settings
{
  float pulsesPerLiterIn;
  float pulsesPerLiterOut;

  float tdsLimit;
  float maxFlushTimeSec;
  float maxRuntimeSec;
  float maxProductionLiters;

  bool  autoStart;
  String apPassword;
  String mqttHost;
  uint16_t mqttPort;
  String mDNSName;
};

extern Settings settings;

/* lädt aus configDoc → settings */
void settingsLoad();

/* schreibt settings → configDoc + speichert */
void settingsSave();
