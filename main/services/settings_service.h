#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum class PaperDisplayDriver : int32_t;

namespace settings_service {

// Firmware default when `/sdcard/portal/config.json` does not specify a driver.
//
// Keep in sync with `docs/config.schema.json`.
PaperDisplayDriver default_display_driver();

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

// Display driver selection (loaded from /sdcard/portal/config.json).
//
// If not configured, `*out_driver` is set to `default_display_driver()` and
// `*out_configured` is false.
esp_err_t get_display_driver(PaperDisplayDriver *out_driver, bool *out_configured);

} // namespace settings_service
