#pragma once

/* ============================================================
   OSMOSE DEFAULT CONFIG VALUES
   ============================================================ */

// Flow
#define DEF_PULSES_PER_LITER_IN      1075.0f
#define DEF_PULSES_PER_LITER_OUT     880.0f

// Process
#define DEF_TDS_LIMIT                13.0f
#define DEF_TDS_MAX_ALLOWED          30.0f

#define DEF_MAX_RUNTIME_AUTO_SEC     2400.0f
#define DEF_MAX_RUNTIME_MANUAL_SEC   3000.0f

#define DEF_MAX_PROD_AUTO_L          35.0f
#define DEF_MAX_PROD_MANUAL_L        45.0f

// System
#define DEF_MQTT_PORT               1883
#define DEF_MDNS_NAME               "osmose"
#define DEF_AP_PASSWORD             "osmoosmo"
#define DEF_MQTT_HOST               "MyRasPi.local"

#define DEF_WIFI_SSID               "VanFranz"
#define DEF_WIFI_PASSWORD           "5032650326"

/* ===== ADD: Flush/Prepare Options ===== */
#define DEF_PREPARE_TIME_SEC       10.0f
#define DEF_AUTOFLUSH_ENABLED      true
#define DEF_AUTOFLUSH_MIN_TIME_SEC   20.0f
#define DEF_MAX_FLUSH_TIME_SEC       180.0f


#define DEF_POSTFLUSH_ENABLED      true
#define DEF_POSTFLUSH_TIME_SEC     10.0f

#define DEF_SERVICE_FLUSH_ENABLED     true
#define DEF_SERVICE_FLUSH_INTERVAL_S  86400  //24h
#define DEF_SERVICE_FLUSH_TIME_S      60
