#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "wasm_export.h"

#include "services/devserver_service.h"
#include "services/settings_service.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_devserver";

int32_t devserverStart(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    bool prev_enabled = false;
    esp_err_t err = settings_service::get_developer_mode(&prev_enabled);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserverStart: nvs read failed");
        return kWasmErrInternal;
    }

    err = settings_service::set_developer_mode(true);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserverStart: nvs write failed");
        return kWasmErrInternal;
    }

    err = devserver::start();
    if (err != ESP_OK) {
        (void)settings_service::set_developer_mode(prev_enabled);
        wasm_api_set_last_error(kWasmErrInternal, "devserverStart: enqueue failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t devserverStop(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    esp_err_t err = devserver::stop();
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserverStop: stop failed");
        return kWasmErrInternal;
    }

    err = settings_service::set_developer_mode(false);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserverStop: nvs write failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t devserverIsRunning(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return devserver::is_running() ? 1 : 0;
}

int32_t devserverIsStarting(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return devserver::is_starting() ? 1 : 0;
}

int32_t devserverGetUrl(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetUrl: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetUrl: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_url(out, (size_t)out_len);
}

int32_t devserverGetApSsid(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetApSsid: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetApSsid: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_ap_ssid(out, (size_t)out_len);
}

int32_t devserverGetApPassword(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetApPassword: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetApPassword: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_ap_password(out, (size_t)out_len);
}

int32_t devserverGetLastError(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetLastError: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserverGetLastError: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_last_error(out, (size_t)out_len);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_devserver_native_symbols[] = {
    REG_NATIVE_FUNC(devserverStart, "()i"),
    REG_NATIVE_FUNC(devserverStop, "()i"),
    REG_NATIVE_FUNC(devserverIsRunning, "()i"),
    REG_NATIVE_FUNC(devserverIsStarting, "()i"),
    REG_NATIVE_FUNC(devserverGetUrl, "(*~)i"),
    REG_NATIVE_FUNC(devserverGetApSsid, "(*~)i"),
    REG_NATIVE_FUNC(devserverGetApPassword, "(*~)i"),
    REG_NATIVE_FUNC(devserverGetLastError, "(*~)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_devserver(void)
{
    const uint32_t count = sizeof(g_devserver_native_symbols) / sizeof(g_devserver_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_devserver", g_devserver_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_devserver natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_devserver: wasm_runtime_register_natives failed");
    }
    return ok;
}
