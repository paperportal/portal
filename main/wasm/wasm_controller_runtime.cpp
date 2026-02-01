#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "api.h"
#include "wasm_controller.h"

static constexpr const char *kTag = "wasm_controller";

bool WasmController::Init()
{
    if (runtime_initialized_) {
        return true;
    }

    ESP_LOGI(kTag, "Initialize WAMR");
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(init_args));

    bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        wamr_heap_ = (uint8_t *)heap_caps_malloc(kWamrHeapSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!wamr_heap_) {
        wamr_heap_ = (uint8_t *)heap_caps_malloc(kWamrHeapSize, MALLOC_CAP_8BIT);
        psram_ready = false;
    }

    if (wamr_heap_) {
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = wamr_heap_;
        init_args.mem_alloc_option.pool.heap_size = kWamrHeapSize;

        if (!wasm_runtime_full_init(&init_args)) {
            ESP_LOGE(kTag, "Failed to init WAMR with pool allocator");
            heap_caps_free(wamr_heap_);
            wamr_heap_ = nullptr;
            return false;
        }
        ESP_LOGI(kTag, "WAMR heap pool=%u bytes (%s)", (unsigned)kWamrHeapSize, psram_ready ? "psram" : "internal");
    } else {
        ESP_LOGW(kTag, "Failed to allocate WAMR heap pool; using default allocator");
        if (!wasm_runtime_init()) {
            ESP_LOGE(kTag, "Failed to init WAMR");
            return false;
        }
    }

    if (!wasm_api_register_all()) {
        ESP_LOGE(kTag, "Failed to register wasm native APIs");
        wasm_runtime_destroy();
        if (wamr_heap_) {
            heap_caps_free(wamr_heap_);
            wamr_heap_ = nullptr;
        }
        return false;
    }

    runtime_initialized_ = true;
    return true;
}

void WasmController::Shutdown()
{
    UnloadModule();

    if (runtime_initialized_) {
        wasm_runtime_destroy();
        runtime_initialized_ = false;
    }

    if (wamr_heap_) {
        heap_caps_free(wamr_heap_);
        wamr_heap_ = nullptr;
    }
}

