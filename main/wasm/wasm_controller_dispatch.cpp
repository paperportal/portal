#include <inttypes.h>
#include <string.h>

#include "esp_log.h"

#include "services/devserver_service.h"
#include "wasm/app_contract.h"
#include "wasm_controller.h"

static constexpr const char *kTag = "wasm_controller";

bool WasmController::CallWasm(wasm_function_inst_t func, uint32_t argc, uint32_t *argv, const char *name)
{
    if (!exec_env_ || !inst_ || !func) {
        return false;
    }

    if (!wasm_runtime_call_wasm(exec_env_, func, argc, argv)) {
        const char *exception = wasm_runtime_get_exception(inst_);
        ESP_LOGE(kTag, "WASM call failed (%s): %s", name ? name : "(unknown)", exception ? exception : "(no exception)");
        if (exception) {
            devserver::notify_uploaded_crashed(exception);
        }
        DisableDispatch("exception");
        return false;
    }

    return true;
}

void WasmController::DisableDispatch(const char *reason)
{
    if (dispatch_enabled_) {
        ESP_LOGE(kTag, "Disabling wasm dispatch (%s)", reason ? reason : "unknown");
        dispatch_enabled_ = false;
    }
}

bool WasmController::CallShutdown()
{
    if (!inst_ || !exec_env_ || !exports_.shutdown) {
        return false;
    }

    uint32_t argv[1] = { 0 };
    if (!wasm_runtime_call_wasm(exec_env_, exports_.shutdown, 0, argv)) {
        const char *exception = wasm_runtime_get_exception(inst_);
        ESP_LOGW(kTag, "ppShutdown failed: %s", exception ? exception : "(no exception)");
        return false;
    }
    return true;
}

bool WasmController::CallInit(int32_t api_version,
    int32_t args_ptr, int32_t args_len)
{
    if (!dispatch_enabled_ || !exports_.init) {
        return false;
    }

    uint32_t argv[3];
    argv[0] = (uint32_t)api_version;
    argv[1] = (uint32_t)args_ptr;
    argv[2] = (uint32_t)args_len;

    if (!CallWasm(exports_.init, 3, argv, pp_contract::kExportInit)) {
        return false;
    }
    return true;
}

bool WasmController::CallTick(int32_t now_ms)
{
    if (!dispatch_enabled_ || !exports_.tick) {
        return false;
    }
    uint32_t argv[1] = { (uint32_t)now_ms };
    return CallWasm(exports_.tick, 1, argv, pp_contract::kExportTick);
}

bool WasmController::CallOnGesture(int32_t kind, int32_t x, int32_t y, int32_t dx, int32_t dy,
    int32_t duration_ms, int32_t now_ms, int32_t flags)
{
    if (!dispatch_enabled_ || !exports_.on_gesture) {
        return false;
    }
    uint32_t argv[8] = { (uint32_t)kind, (uint32_t)x, (uint32_t)y, (uint32_t)dx, (uint32_t)dy,
        (uint32_t)duration_ms, (uint32_t)now_ms, (uint32_t)flags };
    return CallWasm(exports_.on_gesture, 8, argv, pp_contract::kExportOnGesture);
}

bool WasmController::CallOnHttpRequest(int32_t req_id, int32_t method, int32_t uri_ptr, int32_t uri_len,
    int32_t body_ptr, int32_t body_len, int32_t content_len, int32_t now_ms, int32_t flags)
{
    if (!dispatch_enabled_ || !exports_.on_http_request) {
        return false;
    }
    uint32_t argv[9] = { (uint32_t)req_id, (uint32_t)method, (uint32_t)uri_ptr, (uint32_t)uri_len,
        (uint32_t)body_ptr, (uint32_t)body_len, (uint32_t)content_len, (uint32_t)now_ms, (uint32_t)flags };
    return CallWasm(exports_.on_http_request, 9, argv, pp_contract::kExportOnHttpRequest);
}

bool WasmController::CallOnWifiEvent(int32_t kind, int32_t now_ms, int32_t arg0, int32_t arg1)
{
    if (!dispatch_enabled_ || !exports_.on_wifi_event) {
        return false;
    }

    uint32_t argv[4] = { (uint32_t)kind, (uint32_t)now_ms, (uint32_t)arg0, (uint32_t)arg1 };
    return CallWasm(exports_.on_wifi_event, 4, argv, pp_contract::kExportOnWifiEvent);
}

int32_t WasmController::CallAlloc(int32_t len)
{
    if (!dispatch_enabled_ || !exports_.alloc) {
        return 0;
    }

    uint32_t argv[1] = { (uint32_t)len };
    if (!CallWasm(exports_.alloc, 1, argv, pp_contract::kExportAlloc)) {
        return 0;
    }

    return (int32_t)argv[0];
}

void WasmController::CallFree(int32_t ptr, int32_t len)
{
    if (!dispatch_enabled_ || !exports_.free) {
        return;
    }

    uint32_t argv[2] = { (uint32_t)ptr, (uint32_t)len };
    CallWasm(exports_.free, 2, argv, pp_contract::kExportFree);
}
