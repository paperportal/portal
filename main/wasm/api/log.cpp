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

void logInfo(wasm_exec_env_t exec_env, const char *msg)
{
    (void)exec_env;
    if (!msg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "logInfo: msg is null");
        ESP_LOGW(kTag, "wasm logInfo called with null msg");
        return;
    }
    ESP_LOGI(kTag, "%s", msg);
    devserver::log_pushf("I %s", msg);
}

void logWarn(wasm_exec_env_t exec_env, const char *msg)
{
    (void)exec_env;
    if (!msg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "logWarn: msg is null");
        ESP_LOGW(kTag, "wasm logWarn called with null msg");
        return;
    }
    ESP_LOGW(kTag, "%s", msg);
    devserver::log_pushf("W %s", msg);
}

void logError(wasm_exec_env_t exec_env, const char *msg)
{
    (void)exec_env;
    if (!msg) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "logError: msg is null");
        ESP_LOGW(kTag, "wasm logError called with null msg");
        return;
    }
    ESP_LOGE(kTag, "%s", msg);
    devserver::log_pushf("E %s", msg);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, nullptr }

static NativeSymbol g_log_native_symbols[] = {
    REG_NATIVE_FUNC(logInfo, "($)"),
    REG_NATIVE_FUNC(logWarn, "($)"),
    REG_NATIVE_FUNC(logError, "($)"),
};
/* clang-format on */

bool wasm_api_register_log(void)
{
    const uint32_t count = sizeof(g_log_native_symbols) / sizeof(g_log_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_log", g_log_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_log natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_log: wasm_runtime_register_natives failed");
    }
    return ok;
}
