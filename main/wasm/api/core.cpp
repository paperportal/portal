#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "../api.h"
#include "../wasm_controller.h"
#include "m5papers3_display.h"
#include "other/mem_utils.h"
#include "errors.h"
#include "features.h"
#include "host/event_loop.h"

namespace {

constexpr const char *kTag = "wasm_api";

constexpr int32_t kApiVersion = 1;
constexpr int64_t kApiFeatures =
    (int64_t)(kWasmFeatureCore | kWasmFeatureM5 | kWasmFeatureDisplayBasics | kWasmFeatureDisplayPrimitives
        | kWasmFeatureDisplayText | kWasmFeatureDisplayImages | kWasmFeatureTouch | kWasmFeatureFastEPD | kWasmFeatureSpeaker
        | kWasmFeatureRTC | kWasmFeaturePower | kWasmFeatureIMU | kWasmFeatureNet | kWasmFeatureHttp | kWasmFeatureHttpd
        | kWasmFeatureSocket | kWasmFeatureFS | kWasmFeatureNVS | kWasmFeatureDevServer);

int32_t g_last_error_code = 0;
char g_last_error_message[128] = "";

int32_t apiVersion(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return kApiVersion;
}

int64_t apiFeatures(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return kApiFeatures;
}

int32_t lastErrorCode(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return wasm_api_get_last_error_code();
}

int32_t lastErrorMessage(wasm_exec_env_t exec_env, char *out, size_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "lastErrorMessage: out is null");
        return kWasmErrInvalidArgument;
    }

    const char *msg = wasm_api_get_last_error_message();
    const size_t msg_len = strlen(msg);

    if (out_len == 0) {
        return 0;
    }

    const size_t bytes_to_copy = (msg_len < (out_len - 1)) ? msg_len : (out_len - 1);
    memcpy(out, msg, bytes_to_copy);
    out[bytes_to_copy] = '\0';
    return (int32_t)bytes_to_copy;
}

int32_t heapCheck(wasm_exec_env_t exec_env, const char *label, int32_t print_errors)
{
    (void)exec_env;
    if (!label) {
        label = "wasm";
    }
    const bool ok = mem_utils::check_heap_integrity(kTag, label, print_errors != 0);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "check_heap_integrity: heap corruption detected");
    }
    return ok ? 1 : 0;
}

void heapLog(wasm_exec_env_t exec_env, const char *label)
{
    (void)exec_env;
    if (!label) {
        label = "wasm";
    }
    mem_utils::log_heap_brief(kTag, label);
}

int32_t openApp(wasm_exec_env_t exec_env, const char *app_id, const char *arguments)
{
    (void)exec_env;

    // Validate app_id
    if (!app_id) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "openApp: app_id is null");
        return kWasmErrInvalidArgument;
    }

    // Request the app switch (will be processed after current event dispatch completes)
    bool ok = host_event_loop_request_app_switch(app_id, arguments);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrNotFound, "openApp: failed to request app switch");
        return kWasmErrNotFound;
    }

    return kWasmOk;
}

int32_t exitApp(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    bool ok = host_event_loop_request_app_exit();
    if (!ok) {
        wasm_api_set_last_error(kWasmErrNotReady, "exitApp: failed to request app exit");
        return kWasmErrNotReady;
    }

    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_core_native_symbols[] = {
    REG_NATIVE_FUNC(apiVersion, "()i"),
    REG_NATIVE_FUNC(apiFeatures, "()I"),
    REG_NATIVE_FUNC(lastErrorCode, "()i"),
    REG_NATIVE_FUNC(lastErrorMessage, "(*~)i"),
    REG_NATIVE_FUNC(heapCheck, "($i)i"),
    REG_NATIVE_FUNC(heapLog, "($)"),
    REG_NATIVE_FUNC(openApp, "($$)i"),
    REG_NATIVE_FUNC(exitApp, "()i"),
};
/* clang-format on */

} // namespace

void wasm_api_clear_last_error(void)
{
    g_last_error_code = 0;
    g_last_error_message[0] = '\0';
}

void wasm_api_set_last_error(int32_t code, const char *message)
{
    g_last_error_code = code;
    if (!message) {
        snprintf(g_last_error_message, sizeof(g_last_error_message), "unknown error (%" PRIi32 ")", code);
        return;
    }
    snprintf(g_last_error_message, sizeof(g_last_error_message), "%s", message);
}

int32_t wasm_api_get_last_error_code(void)
{
    return g_last_error_code;
}

const char *wasm_api_get_last_error_message(void)
{
    return g_last_error_message;
}

bool wasm_api_register_core(void)
{
    const uint32_t count = sizeof(g_core_native_symbols) / sizeof(g_core_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5", g_core_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5 core natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_core: wasm_runtime_register_natives failed");
    }
    return ok;
}

bool wasm_api_register_all(void)
{
    return wasm_api_register_core()
        && wasm_api_register_display()
        && wasm_api_register_display_images()
        && wasm_api_register_display_primitives()
        && wasm_api_register_display_text()
        && wasm_api_register_fs()
        && wasm_api_register_hal()
        && wasm_api_register_http()
        && wasm_api_register_httpd()
        && wasm_api_register_imu()
        && wasm_api_register_devserver()
        && wasm_api_register_log()
        && wasm_api_register_m5()
        && wasm_api_register_net()
        && wasm_api_register_nvs()
        && wasm_api_register_power()
        && wasm_api_register_rtc()
        && wasm_api_register_socket()
        && wasm_api_register_speaker()
        && wasm_api_register_touch()
        && wasm_api_register_gesture()
        ;
}
