#include "services/settings_service.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "sd_card.h"
#include "wasm/api/display.h"

namespace settings_service {

namespace {

constexpr const char *kNamespace = "paper_portal";
constexpr const char *kKeyDeveloperMode = "developer_mode";
constexpr const char *kSettingsPath = "/sdcard/portal/config.json";
constexpr const char *kTag = "settings_service";

esp_err_t read_settings_json_from_sd(cJSON **out_json)
{
    if (!out_json) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = nullptr;

    // Ensure SD card is mounted
    if (!sd_card_is_mounted()) {
        ESP_LOGI(kTag, "SD card not mounted, skipping settings load");
        return ESP_OK;
    }

    FILE *f = fopen(kSettingsPath, "r");
    if (!f) {
        ESP_LOGI(kTag, "No settings file found at %s", kSettingsPath);
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 4096) {
        ESP_LOGW(kTag, "Invalid settings file size: %ld", file_size);
        fclose(f);
        return ESP_OK;
    }

    char *json_buf = (char *)malloc(file_size + 1);
    if (!json_buf) {
        ESP_LOGW(kTag, "Failed to allocate buffer for settings");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(json_buf, 1, file_size, f);
    json_buf[read_len] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(json_buf);
    free(json_buf);

    if (!json) {
        ESP_LOGW(kTag, "Failed to parse settings JSON");
        return ESP_OK;
    }

    *out_json = json;
    return ESP_OK;
}

} // namespace

PaperDisplayDriver default_display_driver()
{
    return PaperDisplayDriver::fastepd;
}

esp_err_t get_developer_mode(bool *out_enabled)
{
    if (!out_enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_enabled = false;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t v = 0;
    err = nvs_get_u8(handle, kKeyDeveloperMode, &v);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    *out_enabled = (v != 0);
    return ESP_OK;
}

esp_err_t set_developer_mode(bool enabled)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, kKeyDeveloperMode, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t load_wifi_settings_from_sd(WifiSettings *out_settings)
{
    if (!out_settings) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize with empty settings
    memset(out_settings, 0, sizeof(WifiSettings));
    out_settings->configured = false;

    cJSON *json = nullptr;
    esp_err_t err = read_settings_json_from_sd(&json);
    if (err != ESP_OK) {
        return err;
    }
    if (!json) {
        return ESP_OK;
    }

    // Extract WiFi settings
    cJSON *wifi_obj = cJSON_GetObjectItem(json, "wifi");
    if (wifi_obj && cJSON_IsObject(wifi_obj)) {
        cJSON *ssid_val = cJSON_GetObjectItem(wifi_obj, "ssid");
        cJSON *password_val = cJSON_GetObjectItem(wifi_obj, "password");

        if (ssid_val && cJSON_IsString(ssid_val) && ssid_val->valuestring) {
            const char *ssid = ssid_val->valuestring;
            if (ssid[0] != '\0') {
                strncpy(out_settings->ssid, ssid, sizeof(out_settings->ssid) - 1);
                out_settings->ssid[sizeof(out_settings->ssid) - 1] = '\0';
                out_settings->configured = true;
            }
        }

        if (password_val && cJSON_IsString(password_val) && password_val->valuestring) {
            const char *password = password_val->valuestring;
            strncpy(out_settings->password, password, sizeof(out_settings->password) - 1);
            out_settings->password[sizeof(out_settings->password) - 1] = '\0';
        }
    }

    cJSON_Delete(json);

    if (out_settings->configured) {
        ESP_LOGI(kTag, "Loaded WiFi settings: SSID='%s'", out_settings->ssid);
    } else {
        ESP_LOGI(kTag, "No WiFi SSID configured in settings");
    }

    return ESP_OK;
}

esp_err_t get_wifi_settings(WifiSettings *out_settings)
{
    if (!out_settings) {
        return ESP_ERR_INVALID_ARG;
    }

    // Load from SD card
    return load_wifi_settings_from_sd(out_settings);
}

esp_err_t get_display_driver(PaperDisplayDriver *out_driver, bool *out_configured)
{
    if (!out_driver || !out_configured) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_driver = default_display_driver();
    *out_configured = false;

    cJSON *json = nullptr;
    esp_err_t err = read_settings_json_from_sd(&json);
    if (err != ESP_OK) {
        return err;
    }
    if (!json) {
        return ESP_OK;
    }

    cJSON *display_obj = cJSON_GetObjectItem(json, "display");
    if (display_obj && cJSON_IsObject(display_obj)) {
        cJSON *driver_val = cJSON_GetObjectItem(display_obj, "driver");
        if (driver_val && cJSON_IsString(driver_val) && driver_val->valuestring) {
            const char *driver_str = driver_val->valuestring;
            if (strcmp(driver_str, "fastepd") == 0) {
                *out_driver = PaperDisplayDriver::fastepd;
                *out_configured = true;
            } else if (strcmp(driver_str, "lgfx") == 0) {
                *out_driver = PaperDisplayDriver::lgfx;
                *out_configured = true;
            } else {
                ESP_LOGW(kTag, "Unknown display.driver '%s' (expected 'fastepd' or 'lgfx')", driver_str);
            }
        }
    }

    cJSON_Delete(json);
    return ESP_OK;
}

} // namespace settings_service
