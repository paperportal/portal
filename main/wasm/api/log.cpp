#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "services/devserver_service.h"
#include "../api.h"
#include "errors.h"

constexpr const char *kTag = "wasm";

void log_info(wasm_exec_env_t exec_env, const char *msg)
{
    (void)exec_env;
    if (!msg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "log_info: msg is null");
        ESP_LOGW(kTag, "wasm log_info called with null msg");
        return;
    }
    ESP_LOGI(kTag, "%s", msg);
    devserver::log_pushf("I %s", msg);
}

void log_warn(wasm_exec_env_t exec_env, const char *msg)
{
    (void)exec_env;
    if (!msg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "log_warn: msg is null");
        ESP_LOGW(kTag, "wasm log_warn called with null msg");
        return;
    }
    ESP_LOGW(kTag, "%s", msg);
    devserver::log_pushf("W %s", msg);
}

void log_error(wasm_exec_env_t exec_env, const char *msg)
{
    (void)exec_env;
    if (!msg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "log_error: msg is null");
        ESP_LOGW(kTag, "wasm log_error called with null msg");
        return;
    }
    ESP_LOGE(kTag, "%s", msg);
    devserver::log_pushf("E %s", msg);
}

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, (void *)func_name, signature, nullptr }

static NativeSymbol g_log_native_symbols[] = {
    REG_NATIVE_FUNC(log_info, "($)"),
    REG_NATIVE_FUNC(log_warn, "($)"),
    REG_NATIVE_FUNC(log_error, "($)"),
};
/* clang-format on */

bool wasm_api_register_log(void)
{
    const uint32_t count = sizeof(g_log_native_symbols) / sizeof(g_log_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_log", g_log_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_log natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_log: wasm_runtime_register_natives failed");
    }
    return ok;
}
