#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "wasm_export.h"

class WasmController {
public:
    bool Init();
    bool LoadEntrypoint();
    bool LoadEmbeddedEntrypoint();
    bool LoadEmbeddedSettings();
    bool LoadFromBytes(const uint8_t *bytes, size_t len, const char *args, char *error, size_t error_len);
    bool Instantiate();
    bool Instantiate(char *error, size_t error_len);
    void UnloadModule();
    void Shutdown();

    bool CallInit(int32_t api_version, int64_t api_features, int32_t screen_w, int32_t screen_h,
        int32_t args_ptr = 0, int32_t args_len = 0);
    bool CallTick(int32_t now_ms);
    bool CallOnGesture(int32_t kind, int32_t x, int32_t y, int32_t dx, int32_t dy, int32_t duration_ms,
        int32_t now_ms, int32_t flags);
    bool CallOnHttpRequest(int32_t req_id, int32_t method, int32_t uri_ptr, int32_t uri_len, int32_t body_ptr,
        int32_t body_len, int32_t content_len, int32_t now_ms, int32_t flags);
    bool CallOnWifiEvent(int32_t kind, int32_t now_ms, int32_t arg0, int32_t arg1);
    bool CallShutdown();

    int32_t CallAlloc(int32_t len);
    void CallFree(int32_t ptr, int32_t len);
    bool WriteAppMemory(int32_t app_ptr, const void *src, uint32_t len);
    void *GetAppMemory(int32_t app_ptr, uint32_t len);

    bool IsReady() const { return inst_ != nullptr; }
    bool CanDispatch() const { return dispatch_enabled_; }
    bool HasGestureHandler() const { return exports_.on_gesture != nullptr; }
    bool HasHttpRequestHandler() const { return exports_.on_http_request != nullptr; }
    bool HasWifiEventHandler() const { return exports_.on_wifi_event != nullptr; }

private:
    struct Exports {
        wasm_function_inst_t contract_version = nullptr;
        wasm_function_inst_t init = nullptr;
        wasm_function_inst_t tick = nullptr;
        wasm_function_inst_t alloc = nullptr;
        wasm_function_inst_t free = nullptr;
        wasm_function_inst_t on_gesture = nullptr;
        wasm_function_inst_t on_http_request = nullptr;
        wasm_function_inst_t on_wifi_event = nullptr;
        wasm_function_inst_t shutdown = nullptr;
    } exports_{};

    bool lookup_exports();
    bool verify_contract();
    bool call_wasm(wasm_function_inst_t func, uint32_t argc, uint32_t *argv, const char *name);
    void disable_dispatch(const char *reason);
    void set_wasi_args_from_string(const char *args);

    uint8_t *wamr_heap_ = nullptr;
    uint8_t *wasm_module_buf_ = nullptr;
    wasm_module_t module_ = nullptr;
    wasm_module_inst_t inst_ = nullptr;
    wasm_exec_env_t exec_env_ = nullptr;
    bool runtime_initialized_ = false;
    bool dispatch_enabled_ = true;
    std::vector<std::string> wasi_args_;
    std::vector<const char *> wasi_argv_;

    static constexpr size_t kWamrHeapSize = 2 * 1024 * 1024;
    static constexpr size_t kWamrExecEnvStackSize = 16 * 1024;
    static constexpr size_t kWamrWasmStackSize = 16 * 1024;
    static constexpr size_t kWamrWasmHeapSize = 1 * 1024 * 1024;
    // Notes:
    // - For "max memory", consider sizing kWamrHeapSize from free PSRAM at runtime,
    //   leaving a safety reserve for display/system tasks.
    // - If the wasm module exports malloc/free (libc heap), set kWamrWasmHeapSize = 0
    //   to disable the app heap and reduce WAMR global heap pressure.
};

// Global controller accessor for native APIs
void wasm_api_set_controller(WasmController *controller);
WasmController *wasm_api_get_controller(void);
