#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "wasm_export.h"

/** @brief Owns the WAMR runtime/module/instance and provides a small façade for calling the Paper Portal WASM app contract exports. */
class WasmController {
public:
    /** @brief Initialize the WAMR runtime and register native APIs (idempotent). */
    bool Init();

    /** @brief Load the main entrypoint module (SD card override if present, else embedded). */
    bool LoadEntrypoint();

    /** @brief Load the embedded entrypoint module. */
    bool LoadEmbeddedEntrypoint();

    /** @brief Load the embedded settings module. */
    bool LoadEmbeddedSettings();

    /**
     * @brief Load a module from a provided byte buffer, replacing any currently loaded module.
     * @param bytes Pointer to a WASM module buffer.
     * @param len Length of @p bytes in bytes.
     * @param args Optional WASI argv string (space-delimited).
     * @param error Optional output buffer for an error message.
     * @param error_len Length of @p error in bytes.
     * @return true on success.
     */
    bool LoadFromBytes(const uint8_t *bytes, size_t len, const char *args, char *error, size_t error_len);

    /**
     * @brief Load a module from a file on the host filesystem, replacing any currently loaded module.
     * @param abs_path Absolute host path to the WASM module (e.g. "/sdcard/portal/apps/<id>/app.wasm").
     * @param wasi_args Optional WASI argv string (space-delimited).
     * @param error Optional output buffer for an error message.
     * @param error_len Length of @p error in bytes.
     * @return true on success.
     */
    bool LoadFromFile(const char *abs_path, const char *wasi_args, char *error, size_t error_len);

    /** @brief Instantiate the currently loaded module using default error handling. */
    bool Instantiate();

    /**
     * @brief Instantiate the currently loaded module.
     * @param error Optional output buffer for an error message.
     * @param error_len Length of @p error in bytes.
     * @return true on success.
     */
    bool Instantiate(char *error, size_t error_len);

    /** @brief Unload any loaded module and free associated memory. */
    void UnloadModule();

    /** @brief Destroy runtime state and free the WAMR heap pool if allocated. */
    void Shutdown();

    /** @brief Call `ppInit` in the WASM module. */
    bool CallInit(int32_t api_version, int32_t args_ptr = 0, int32_t args_len = 0);

    /** @brief Call `portalMicroTaskStep` in the WASM module. */
    bool CallMicroTaskStep(int32_t handle, int32_t now_ms, int64_t *out_action);

    /** @brief Call `ppOnGesture` in the WASM module. */
    bool CallOnGesture(int32_t kind, int32_t x, int32_t y, int32_t dx, int32_t dy, int32_t duration_ms,
        int32_t now_ms, int32_t flags);

    /** @brief Call `ppOnHttpRequest` in the WASM module. */
    bool CallOnHttpRequest(int32_t req_id, int32_t method, int32_t uri_ptr, int32_t uri_len, int32_t body_ptr,
        int32_t body_len, int32_t content_len, int32_t now_ms, int32_t flags);

    /** @brief Call `ppOnWifiEvent` in the WASM module. */
    bool CallOnWifiEvent(int32_t kind, int32_t now_ms, int32_t arg0, int32_t arg1);

    /** @brief Call `ppShutdown` in the WASM module. */
    bool CallShutdown();

    /** @brief Call the module's allocator export to reserve a region in app memory. */
    int32_t CallAlloc(int32_t len);

    /** @brief Call the module's free export to release a region in app memory. */
    void CallFree(int32_t ptr, int32_t len);

    /** @brief Copy bytes from native memory into the module's linear memory at @p app_ptr. */
    bool WriteAppMemory(int32_t app_ptr, const void *src, uint32_t len);

    /** @brief Map a region of module linear memory into native address space. */
    void *GetAppMemory(int32_t app_ptr, uint32_t len);

    /** @brief True if a module instance has been created. */
    bool IsReady() const { return inst_ != nullptr; }

    /** @brief True if event dispatch into WASM is enabled. */
    bool CanDispatch() const { return dispatch_enabled_; }

    /** @brief True if the module exports a gesture handler. */
    bool HasGestureHandler() const { return exports_.on_gesture != nullptr; }

    /** @brief True if the module exports an HTTP request handler. */
    bool HasHttpRequestHandler() const { return exports_.on_http_request != nullptr; }

    /** @brief True if the module exports a Wi-Fi event handler. */
    bool HasWifiEventHandler() const { return exports_.on_wifi_event != nullptr; }

    /** @brief True if the module exports a microtask step handler. */
    bool HasMicroTaskStepHandler() const { return exports_.microtask_step != nullptr; }

private:
    /** @brief Cached function pointers for WASM exports used by the app contract. */
    struct Exports {
        /** @brief Export: contract version getter. */
        wasm_function_inst_t contract_version = nullptr;
        /** @brief Export: init entrypoint. */
        wasm_function_inst_t init = nullptr;
        /** @brief Export: periodic tick callback. */
        wasm_function_inst_t tick = nullptr;
        /** @brief Export: microtask step callback. */
        wasm_function_inst_t microtask_step = nullptr;
        /** @brief Export: allocator entrypoint. */
        wasm_function_inst_t alloc = nullptr;
        /** @brief Export: free entrypoint. */
        wasm_function_inst_t free = nullptr;
        /** @brief Export: gesture callback. */
        wasm_function_inst_t on_gesture = nullptr;
        /** @brief Export: HTTP request callback. */
        wasm_function_inst_t on_http_request = nullptr;
        /** @brief Export: Wi-Fi event callback. */
        wasm_function_inst_t on_wifi_event = nullptr;
        /** @brief Export: shutdown callback. */
        wasm_function_inst_t shutdown = nullptr;
    } exports_{};

    /**
     * @brief Allocate and assign @c wasm_module_buf_ for a module of @p len bytes.
     *        Prefers PSRAM when available.
     */
    bool AllocateWasmModuleBuffer(size_t len);
    /**
     * @brief Load a module from @c wasm_module_buf_ and configure WASI args.
     *        Takes ownership of @c wasm_module_buf_ and frees it on failure.
     */

     bool LoadModuleFromOwnedBuffer(size_t len, const char *args, char *error, size_t error_len);

    /** @brief Resolve required exports from the instantiated module. */
    bool LookupExports();

    /** @brief Validate the module's contract version export matches firmware expectations. */
    bool VerifyContract();

    /** @brief Call a WASM function and handle exceptions/dispatch disabling. */
    bool CallWasm(wasm_function_inst_t func, uint32_t argc, uint32_t *argv, const char *name);

    /** @brief Disable future dispatch into WASM (e.g., after an exception). */
    void DisableDispatch(const char *reason);

    /** @brief Parse a space-delimited args string into @c wasi_argv_. */
    void SetWasiArgsFromString(const char *args);

    /** @brief Optional WAMR heap pool (PSRAM preferred) used by the runtime allocator. */
    uint8_t *wamr_heap_ = nullptr;

    /** @brief Owned module bytes buffer backing @c module_. */
    uint8_t *wasm_module_buf_ = nullptr;

    /** @brief Loaded module handle. */
    wasm_module_t module_ = nullptr;

    /** @brief Instantiated module handle. */
    wasm_module_inst_t inst_ = nullptr;

    /** @brief Execution environment used for calls into WASM. */
    wasm_exec_env_t exec_env_ = nullptr;

    /** @brief True once the WAMR runtime has been initialized. */
    bool runtime_initialized_ = false;

    /** @brief Enables/disables event dispatch into the module. */
    bool dispatch_enabled_ = true;

    /** @brief Backing storage for parsed WASI arguments. */
    std::vector<std::string> wasi_args_;

    /** @brief C-string argv pointers corresponding to @c wasi_args_. */
    std::vector<const char *> wasi_argv_;

    /** @brief Bytes reserved for the WAMR global heap pool. */
    static constexpr size_t kWamrHeapSize = 2 * 1024 * 1024;

    /** @brief Exec env stack size used by WAMR for running calls. */
    static constexpr size_t kWamrExecEnvStackSize = 16 * 1024;

    /** @brief Wasm stack size requested for module instantiation. */
    static constexpr size_t kWamrWasmStackSize = 16 * 1024;

    /** @brief Wasm heap size requested for module instantiation. */
    static constexpr size_t kWamrWasmHeapSize = 0;

    // Notes:
    // - For "max memory", consider sizing kWamrHeapSize from free PSRAM at runtime,
    //   leaving a safety reserve for display/system tasks.
    // - If the wasm module exports malloc/free (libc heap), set kWamrWasmHeapSize = 0
    //   to disable the host-managed app heap and reduce WAMR global heap pressure.
};

/** @brief Set the global controller pointer used by native WASM APIs. */
void wasm_api_set_controller(WasmController *controller);

/** @brief Get the global controller pointer used by native WASM APIs. */
WasmController *wasm_api_get_controller(void);
