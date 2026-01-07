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

int32_t devserver_start(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    bool prev_enabled = false;
    esp_err_t err = settings_service::get_developer_mode(&prev_enabled);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserver_start: nvs read failed");
        return kWasmErrInternal;
    }

    err = settings_service::set_developer_mode(true);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserver_start: nvs write failed");
        return kWasmErrInternal;
    }

    err = devserver::start();
    if (err != ESP_OK) {
        (void)settings_service::set_developer_mode(prev_enabled);
        wasm_api_set_last_error(kWasmErrInternal, "devserver_start: start failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t devserver_stop(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    esp_err_t err = devserver::stop();
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserver_stop: stop failed");
        return kWasmErrInternal;
    }

    err = settings_service::set_developer_mode(false);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "devserver_stop: nvs write failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

int32_t devserver_is_running(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return devserver::is_running() ? 1 : 0;
}

int32_t devserver_get_url(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserver_get_url: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserver_get_url: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_url(out, (size_t)out_len);
}

int32_t devserver_get_ap_ssid(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserver_get_ap_ssid: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserver_get_ap_ssid: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_ap_ssid(out, (size_t)out_len);
}

int32_t devserver_get_ap_password(wasm_exec_env_t exec_env, char *out, int32_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserver_get_ap_password: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "devserver_get_ap_password: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    return devserver::get_ap_password(out, (size_t)out_len);
}

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, (void *)func_name, signature, NULL }

static NativeSymbol g_devserver_native_symbols[] = {
    REG_NATIVE_FUNC(devserver_start, "()i"),
    REG_NATIVE_FUNC(devserver_stop, "()i"),
    REG_NATIVE_FUNC(devserver_is_running, "()i"),
    REG_NATIVE_FUNC(devserver_get_url, "(*~)i"),
    REG_NATIVE_FUNC(devserver_get_ap_ssid, "(*~)i"),
    REG_NATIVE_FUNC(devserver_get_ap_password, "(*~)i"),
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
