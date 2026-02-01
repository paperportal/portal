#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

#include "wasm_controller.h"

static constexpr const char *kTag = "wasm_controller";

bool WasmController::WriteAppMemory(int32_t app_ptr, const void *src, uint32_t len)
{
    if (!dispatch_enabled_ || !inst_ || !src || len == 0) {
        return false;
    }

    if (!wasm_runtime_validate_app_addr(inst_, (uint64_t)app_ptr, (uint64_t)len)) {
        ESP_LOGE(kTag, "Invalid wasm memory address ptr=%" PRId32 " len=%" PRIu32, app_ptr, len);
        return false;
    }

    void *native = wasm_runtime_addr_app_to_native(inst_, (uint64_t)app_ptr);
    if (!native) {
        ESP_LOGE(kTag, "Failed to map wasm memory ptr=%" PRId32, app_ptr);
        return false;
    }

    memcpy(native, src, len);
    return true;
}

void *WasmController::GetAppMemory(int32_t app_ptr, uint32_t len)
{
    if (!dispatch_enabled_ || !inst_ || len == 0) {
        return nullptr;
    }

    if (!wasm_runtime_validate_app_addr(inst_, (uint64_t)app_ptr, (uint64_t)len)) {
        ESP_LOGE(kTag, "Invalid wasm memory address ptr=%" PRId32 " len=%" PRIu32, app_ptr, len);
        return nullptr;
    }

    return wasm_runtime_addr_app_to_native(inst_, (uint64_t)app_ptr);
}

