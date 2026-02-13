#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "services/devserver_service.h"
#include "services/settings_service.h"
#include "host/event_loop.h"
#include "other/mem_utils.h"
#include "wasm/wasm_controller.h"
#include "sd_card.h"
#include <dirent.h>

static const char *kTag = "paperportal-runner";

extern "C" void show_sleepimage_with_fastepd_best_effort(void);

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

esp_err_t start_dev_server_autostart() {
    bool enabled = false;
    esp_err_t err = settings_service::get_developer_mode(&enabled);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "[start_dev_server_autostart] NVS read failed (%s)", esp_err_to_name(err));
        return err;
    }
    if (!enabled) {
        return ESP_OK;
    }
    ESP_LOGI(kTag, "[start_dev_server_autostart] Enqueueing devserver startup.");
    err = devserver::start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "[start_dev_server_autostart] autostart enqueue failed (%s)", esp_err_to_name(err));
    }
    return err;
}

extern "C" void app_main() {
    ESP_LOGI(kTag, "[app_main] Starting up.");
    mem_utils::init();

    ESP_LOGI(kTag, "[app_main] Application started. Initializing NVS.");
    init_nvs();
    ESP_LOGI(kTag, "[app_main] Mounting SD card.");
    if (sd_card_mount()) {
        ESP_LOGI(kTag, "[app_main] SD card mounted successfully.");
    } else {
        ESP_LOGW(kTag, "[app_main] SD card mount failed or no card present.");
    }

    ESP_LOGI(kTag, "[app_main] Creating WASM controller.");
    static WasmController wasm;
    wasm_api_set_controller(&wasm);
    mem_utils::log_heap_brief(kTag, "[app_main] startup");
    ESP_LOGI(kTag, "[app_main] Starting event loop.");
    if (!host_event_loop_start(&wasm)) {
        ESP_LOGE(kTag, "[app_main] Failed to start host event loop");
        return;
    }
    ESP_LOGI(kTag, "[app_main] Event loop started. Starting devserver if enabled.");
    start_dev_server_autostart();

    ESP_LOGI(kTag, "[app_main] Looping forever...");
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }

}
