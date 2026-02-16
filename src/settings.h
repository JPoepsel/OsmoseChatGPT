#pragma once
#include <Arduino.h>
#include "config_defaults.h"


struct Settings
{
  float pulsesPerLiterIn  = DEF_PULSES_PER_LITER_IN;
  float pulsesPerLiterOut = DEF_PULSES_PER_LITER_OUT;

  float tdsLimit        = DEF_TDS_LIMIT;
  float maxFlushTimeSec = DEF_MAX_FLUSH_TIME_SEC;
  float tdsMaxAllowed   = DEF_TDS_MAX_ALLOWED;

  float maxRuntimeAutoSec   = DEF_MAX_RUNTIME_AUTO_SEC;
  float maxRuntimeManualSec = DEF_MAX_RUNTIME_MANUAL_SEC;

  float maxProductionAutoLiters   = DEF_MAX_PROD_AUTO_L;
  float maxProductionManualLiters = DEF_MAX_PROD_MANUAL_L;

  float prepareTimeSec = DEF_PREPARE_TIME_SEC;
  bool  autoFlushEnabled = DEF_AUTOFLUSH_ENABLED;
  bool  postFlushEnabled = DEF_POSTFLUSH_ENABLED;
  float postFlushTimeSec = DEF_POSTFLUSH_TIME_SEC;
  float autoFlushMinTimeSec = DEF_AUTOFLUSH_MIN_TIME_SEC; 

  bool  serviceFlushEnabled     = DEF_SERVICE_FLUSH_ENABLED;
  uint32_t serviceFlushIntervalSec = DEF_SERVICE_FLUSH_INTERVAL_S;
  uint32_t serviceFlushTimeSec     = DEF_SERVICE_FLUSH_TIME_S;

  String mqttHost     = DEF_MQTT_HOST;
  uint16_t mqttPort   = DEF_MQTT_PORT;
  String mDNSName     = DEF_MDNS_NAME;
  String apPassword   = DEF_AP_PASSWORD;
  String wifiSSID     = DEF_WIFI_SSID;
  String wifiPassword = DEF_WIFI_PASSWORD;
};

extern Settings settings;

/* lädt aus configDoc → settings */
void settingsLoad();

/* schreibt settings → configDoc + speichert */
void settingsSave();
