#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

namespace settings_service {

// WiFi settings structure
struct WifiSettings {
    char ssid[33];      // SSID (max 32 chars + null terminator)
    char password[65];  // Password (max 64 chars + null terminator)
    bool configured;    // true if SSID is set
};

// Developer mode settings
esp_err_t get_developer_mode(bool *out_enabled);
esp_err_t set_developer_mode(bool enabled);

// WiFi settings (loaded from /sdcard/paperportal.json)
esp_err_t get_wifi_settings(WifiSettings *out_settings);
esp_err_t load_wifi_settings_from_sd(WifiSettings *out_settings);

} // namespace settings_service
