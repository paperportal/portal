#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "host/microtask_scheduler.h"

#include "../api.h"
#include "../wasm_controller.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_microtask";

int32_t microtaskClearAll(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    microtask_scheduler().ClearAll();
    return kWasmOk;
}

int32_t microtaskStart(wasm_exec_env_t exec_env, int32_t start_after_ms, int32_t period_ms, int32_t flags)
{
    (void)exec_env;

    if (start_after_ms < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "microtaskStart: start_after_ms < 0");
        return kWasmErrInvalidArgument;
    }
    if (period_ms < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "microtaskStart: period_ms < 0");
        return kWasmErrInvalidArgument;
    }
    if (flags != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "microtaskStart: flags must be 0");
        return kWasmErrInvalidArgument;
    }

    WasmController *wasm = wasm_api_get_controller();
    if (!wasm || !wasm->IsReady() || !wasm->CanDispatch()) {
        wasm_api_set_last_error(kWasmErrNotReady, "microtaskStart: wasm controller not ready");
        return kWasmErrNotReady;
    }
    if (!wasm->HasMicroTaskStepHandler()) {
        wasm_api_set_last_error(kWasmErrNotReady, "microtaskStart: missing portalMicroTaskStep export");
        return kWasmErrNotReady;
    }

    int32_t handle = microtask_scheduler().Start((uint32_t)start_after_ms, (uint32_t)period_ms);
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInternal, "microtaskStart: no free task slots");
        return kWasmErrInternal;
    }

    return handle;
}

int32_t microtaskCancel(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "microtaskCancel: handle <= 0");
        return kWasmErrInvalidArgument;
    }

    const int32_t rc = microtask_scheduler().Cancel(handle);
    if (rc < 0) {
        wasm_api_set_last_error(kWasmErrNotFound, "microtaskCancel: handle not found");
        return kWasmErrNotFound;
    }

    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_microtask_native_symbols[] = {
    REG_NATIVE_FUNC(microtaskClearAll, "()i"),
    REG_NATIVE_FUNC(microtaskStart, "(iii)i"),
    REG_NATIVE_FUNC(microtaskCancel, "(i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_microtask(void)
{
    const uint32_t count = sizeof(g_microtask_native_symbols) / sizeof(g_microtask_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_microtask", g_microtask_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_microtask natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_microtask: wasm_runtime_register_natives failed");
    }
    return ok;
}
