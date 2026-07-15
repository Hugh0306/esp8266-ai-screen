#pragma once

// ---- Firmware version (shown on the first-time WiFi setup screen & /api/info) ----
#define FW_VERSION "0.5.10"

// ---- Bridge polling ----
#define BRIDGE_DEFAULT_PORT 8765
#define BRIDGE_DEFAULT_PATH "/status"
#define BRIDGE_POLL_INTERVAL_MS 5000
#define BRIDGE_HTTP_TIMEOUT_MS 3000
#define WEATHER_POLL_INTERVAL_MS 60000
#define BRIDGE_RETRY_INTERVAL_MS 15000
#define DIRECT_HTTP_TIMEOUT_MS 3000
#define DIRECT_RESPONSE_MAX_BYTES 4096
#define DIRECT_STOCK_INTERVAL_MS 10000
#define DIRECT_STOCK_RETRY_MIN_MS 15000
#define DIRECT_STOCK_RETRY_MAX_MS 300000
#define DIRECT_WEATHER_INTERVAL_MS 600000
#define DIRECT_WEATHER_RETRY_MIN_MS 15000
#define DIRECT_WEATHER_RETRY_MAX_MS 300000
#define QWEATHER_REQUEST_GAP_MS 3000
#define QWEATHER_API_KEY_MAX_LENGTH 64
#define QWEATHER_API_HOST_MAX_LENGTH 95
#define QWEATHER_GZIP_MAX_BYTES 2048
#define QWEATHER_JSON_MAX_BYTES 4096
#ifndef WEATHER_LOCATION_NAME
#define WEATHER_LOCATION_NAME "Beijing"
#endif
#ifndef WEATHER_LOCATION_LABEL
#define WEATHER_LOCATION_LABEL "BEIJING"
#endif
#ifndef QWEATHER_LONGITUDE
#define QWEATHER_LONGITUDE "116.4074"
#endif
#ifndef QWEATHER_LATITUDE
#define QWEATHER_LATITUDE "39.9042"
#endif
#define LEGACY_CAIYUN_TOKEN_MAX_LENGTH 64
#define STOCK_STALE_AFTER_S 120
#define WEATHER_STALE_AFTER_S 1800
#define OFFLINE_CACHE_SAVE_INTERVAL_MS 1800000
#define OFFLINE_CACHE_RETRY_MS 60000

// ---- WiFiManager ----
#define WIFI_PORTAL_AP_NAME "AI-Clock-Setup"
#define WIFI_CONFIG_FILE "/bridge_host.txt"
#define WIFI_CONFIG_TMP_FILE "/bridge_host.tmp"
#define WIFI_CONFIG_BAK_FILE "/bridge_host.bak"

// ---- Backlight ----
#define BRIGHTNESS_FILE "/brightness.txt"
#define DEVICE_SETTINGS_FILE "/settings.json"
#define DEVICE_SETTINGS_TMP_FILE "/settings.tmp"
#define DEVICE_SETTINGS_BAK_FILE "/settings.bak"
#define OFFLINE_STATE_FILE "/offline.json"
#define OFFLINE_STATE_TMP_FILE "/offline.tmp"
#define OFFLINE_STATE_BAK_FILE "/offline.bak"
#define BRIGHTNESS_DEFAULT 100
#define BRIGHTNESS_PWM_FREQ 2000 // Hz; high enough to avoid visible flicker when dim

// ---- Clock / NTP / night mode ----
#define NTP_PRIMARY_DEFAULT "ntp.aliyun.com"
#define NTP_SECONDARY "ntp.tencent.com"
#define NTP_FALLBACK "pool.ntp.org"
#define CLOCK_TZ "CST-8"
#define NIGHT_START_DEFAULT_MIN 1320 // 22:00
#define NIGHT_END_DEFAULT_MIN 420    // 07:00
#define NIGHT_BRIGHTNESS_DEFAULT 10
#define SCREEN_SCHEDULE_ENABLED_DEFAULT true
#define SCREEN_OFF_START_DEFAULT_MIN 0   // 00:00
#define SCREEN_OFF_END_DEFAULT_MIN 420   // 07:00

// ---- Display layout (240x240 ST7789) ----
#define SCREEN_W 240
#define SCREEN_H 240
