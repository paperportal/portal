#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"

#include "services/devserver_service.h"
#include "sd_card.h"
#include "wasm_controller.h"
#include "wasm/app_contract.h"
#include "api.h"

static constexpr const char *kTag = "wasm_controller";

extern "C" const uint8_t _binary_entrypoint_wasm_start[];
extern "C" const uint8_t _binary_entrypoint_wasm_end[];
extern "C" const uint8_t _binary_settings_wasm_start[];
extern "C" const uint8_t _binary_settings_wasm_end[];

namespace {

static WasmController *g_wasm_controller = nullptr;

} // namespace

void wasm_api_set_controller(WasmController *controller)
{
    g_wasm_controller = controller;
}

WasmController *wasm_api_get_controller(void)
{
    return g_wasm_controller;
}

bool WasmController::Init()
{
    if (runtime_initialized_) {
        return true;
    }

    ESP_LOGI(kTag, "Initialize WAMR");
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(init_args));

    bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        wamr_heap_ = (uint8_t *)heap_caps_malloc(kWamrHeapSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!wamr_heap_) {
        wamr_heap_ = (uint8_t *)heap_caps_malloc(kWamrHeapSize, MALLOC_CAP_8BIT);
        psram_ready = false;
    }

    if (wamr_heap_) {
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = wamr_heap_;
        init_args.mem_alloc_option.pool.heap_size = kWamrHeapSize;

        if (!wasm_runtime_full_init(&init_args)) {
            ESP_LOGE(kTag, "Failed to init WAMR with pool allocator");
            heap_caps_free(wamr_heap_);
            wamr_heap_ = nullptr;
            return false;
        }
        ESP_LOGI(kTag, "WAMR heap pool=%u bytes (%s)", (unsigned)kWamrHeapSize,
            psram_ready ? "psram" : "internal");
    }
    else {
        ESP_LOGW(kTag, "Failed to allocate WAMR heap pool; using default allocator");
        if (!wasm_runtime_init()) {
            ESP_LOGE(kTag, "Failed to init WAMR");
            return false;
        }
    }

    if (!wasm_api_register_all()) {
        ESP_LOGE(kTag, "Failed to register wasm native APIs");
        wasm_runtime_destroy();
        if (wamr_heap_) {
            heap_caps_free(wamr_heap_);
            wamr_heap_ = nullptr;
        }
        return false;
    }

    runtime_initialized_ = true;
    return true;
}

bool WasmController::LoadEntrypoint()
{
    if (!runtime_initialized_) {
        ESP_LOGE(kTag, "LoadEntrypoint called before Init");
        return false;
    }

    if (module_) {
        return true;
    }

    // Try to load /sdcard/portal/entrypoint.wasm first
    const char *mount_point = sd_card_mount_point();
    if (mount_point && sd_card_is_mounted()) {
        char entrypoint_path[256];
        snprintf(entrypoint_path, sizeof(entrypoint_path), "%s/portal/entrypoint.wasm", mount_point);

        struct stat st;
        if (stat(entrypoint_path, &st) == 0 && S_ISREG(st.st_mode)) {
            ESP_LOGI(kTag, "Found entrypoint.wasm at %s (%" PRIu64 " bytes)", entrypoint_path, (uint64_t)st.st_size);

            FILE *f = fopen(entrypoint_path, "rb");
            if (f) {
                size_t file_size = (size_t)st.st_size;

                bool psram_ready = esp_psram_is_initialized();
                if (psram_ready) {
                    wasm_module_buf_ = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
                if (!wasm_module_buf_) {
                    wasm_module_buf_ = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_8BIT);
                }
                if (!wasm_module_buf_) {
                    ESP_LOGE(kTag, "Failed to allocate wasm module buffer (%u bytes)", (unsigned)file_size);
                    fclose(f);
                    // Fall back to embedded entrypoint
                } else {
                    size_t bytes_read = fread(wasm_module_buf_, 1, file_size, f);
                    fclose(f);

                    if (bytes_read != file_size) {
                        ESP_LOGE(kTag, "Failed to read entrypoint.wasm (read %u of %u bytes)",
                                 (unsigned)bytes_read, (unsigned)file_size);
                        heap_caps_free(wasm_module_buf_);
                        wasm_module_buf_ = nullptr;
                        // Fall back to embedded entrypoint
                    } else {
                        char error_buf[1000] = "";
                        module_ = wasm_runtime_load(wasm_module_buf_, file_size, error_buf, sizeof(error_buf));
                        if (!module_) {
                            ESP_LOGE(kTag, "Failed to load entrypoint.wasm -- %s", error_buf);
                            heap_caps_free(wasm_module_buf_);
                            wasm_module_buf_ = nullptr;
                            // Fall back to embedded entrypoint
                        } else {
                            ESP_LOGI(kTag, "Successfully loaded entrypoint.wasm from SD card");
                            set_wasi_args_from_string(nullptr);
                            wasm_runtime_set_wasi_args(module_, wasi_argv_.empty() ? nullptr : wasi_argv_.data(),
                                (uint32_t)wasi_argv_.size(), nullptr, 0, nullptr, 0, nullptr, 0);
                            return true;
                        }
                    }
                }
            }
        } else {
            ESP_LOGI(kTag, "No entrypoint.wasm found at %s, using embedded entrypoint", entrypoint_path);
        }
    } else {
        ESP_LOGI(kTag, "SD card not mounted, using embedded entrypoint");
    }

    // Fall back to embedded entrypoint
    return LoadEmbeddedEntrypoint();
}

bool WasmController::LoadEmbeddedEntrypoint()
{
    if (!runtime_initialized_) {
        ESP_LOGE(kTag, "LoadEntrypoint called before Init");
        return false;
    }

    if (module_) {
        return true;
    }

    const uint8_t *wasm_module = _binary_entrypoint_wasm_start;
    const size_t wasm_module_size = (size_t)(_binary_entrypoint_wasm_end - _binary_entrypoint_wasm_start);
    ESP_LOGI(kTag, "Module size=%u", (unsigned)wasm_module_size);

    bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(wasm_module_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(wasm_module_size, MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        ESP_LOGE(kTag, "Failed to allocate wasm module buffer (%u bytes)", (unsigned)wasm_module_size);
        return false;
    }

    memcpy(wasm_module_buf_, wasm_module, wasm_module_size);

    char error_buf[1000] = "";
    module_ = wasm_runtime_load(wasm_module_buf_, wasm_module_size, error_buf, sizeof(error_buf));
    if (!module_) {
        ESP_LOGE(kTag, "Failed to load wasm module -- %s", error_buf);
        heap_caps_free(wasm_module_buf_);
        wasm_module_buf_ = nullptr;
        return false;
    }

    // Provide empty WASI args/env so WAMR can initialize the WASI environment.
    set_wasi_args_from_string(nullptr);
    wasm_runtime_set_wasi_args(module_, wasi_argv_.empty() ? nullptr : wasi_argv_.data(), (uint32_t)wasi_argv_.size(),
        nullptr, 0, nullptr, 0, nullptr, 0);

    return true;
}

bool WasmController::LoadEmbeddedSettings()
{
    if (!runtime_initialized_) {
        ESP_LOGE(kTag, "LoadEmbeddedSettings called before Init");
        return false;
    }

    if (module_) {
        return true;
    }

    const uint8_t *wasm_module = _binary_settings_wasm_start;
    const size_t wasm_module_size = (size_t)(_binary_settings_wasm_end - _binary_settings_wasm_start);
    ESP_LOGI(kTag, "Settings module size=%u", (unsigned)wasm_module_size);

    bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(wasm_module_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(wasm_module_size, MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        ESP_LOGE(kTag, "Failed to allocate wasm module buffer (%u bytes)", (unsigned)wasm_module_size);
        return false;
    }

    memcpy(wasm_module_buf_, wasm_module, wasm_module_size);

    char error_buf[1000] = "";
    module_ = wasm_runtime_load(wasm_module_buf_, wasm_module_size, error_buf, sizeof(error_buf));
    if (!module_) {
        ESP_LOGE(kTag, "Failed to load wasm module -- %s", error_buf);
        heap_caps_free(wasm_module_buf_);
        wasm_module_buf_ = nullptr;
        return false;
    }

    // Provide empty WASI args/env so WAMR can initialize the WASI environment.
    set_wasi_args_from_string(nullptr);
    wasm_runtime_set_wasi_args(module_, wasi_argv_.empty() ? nullptr : wasi_argv_.data(), (uint32_t)wasi_argv_.size(),
        nullptr, 0, nullptr, 0, nullptr, 0);

    return true;
}

bool WasmController::LoadFromBytes(const uint8_t *bytes, size_t len, const char *args, char *error,
    size_t error_len)
{
    if (!runtime_initialized_) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "LoadFromBytes called before Init");
        }
        return false;
    }

    if (!bytes || len == 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "invalid wasm payload");
        }
        return false;
    }

    UnloadModule();

    bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "alloc failed (%u bytes)", (unsigned)len);
        }
        return false;
    }

    memcpy(wasm_module_buf_, bytes, len);

    char local_error[256] = "";
    char *err_buf = (error && error_len > 0) ? error : local_error;
    size_t err_len = (error && error_len > 0) ? error_len : sizeof(local_error);

    module_ = wasm_runtime_load(wasm_module_buf_, len, err_buf, err_len);
    if (!module_) {
        heap_caps_free(wasm_module_buf_);
        wasm_module_buf_ = nullptr;
        return false;
    }

    set_wasi_args_from_string(args);
    wasm_runtime_set_wasi_args(module_, wasi_argv_.empty() ? nullptr : wasi_argv_.data(), (uint32_t)wasi_argv_.size(),
        nullptr, 0, nullptr, 0, nullptr, 0);

    return true;
}

bool WasmController::Instantiate()
{
    return Instantiate(nullptr, 0);
}

bool WasmController::Instantiate(char *error, size_t error_len)
{
    if (!module_) {
        ESP_LOGE(kTag, "Instantiate called before LoadEntrypoint");
        return false;
    }

    if (inst_) {
        return true;
    }

    char local_error[256] = "";
    char *err_buf = (error && error_len > 0) ? error : local_error;
    size_t err_len = (error && error_len > 0) ? error_len : sizeof(local_error);

    inst_ = wasm_runtime_instantiate(module_, kWamrWasmStackSize, kWamrWasmHeapSize, err_buf, err_len);
    if (!inst_) {
        ESP_LOGE(kTag, "Failed to instantiate wasm module -- %s", err_buf);
        return false;
    }

    exec_env_ = wasm_runtime_create_exec_env(inst_, kWamrExecEnvStackSize);
    if (!exec_env_) {
        ESP_LOGE(kTag, "Failed to create exec env");
        wasm_runtime_deinstantiate(inst_);
        inst_ = nullptr;
        return false;
    }

    if (!lookup_exports()) {
        return false;
    }

    if (!verify_contract()) {
        return false;
    }

    dispatch_enabled_ = true;
    return true;
}

void WasmController::UnloadModule()
{
    dispatch_enabled_ = false;

    exports_ = {};

    if (exec_env_) {
        wasm_runtime_destroy_exec_env(exec_env_);
        exec_env_ = nullptr;
    }

    if (inst_) {
        wasm_runtime_deinstantiate(inst_);
        inst_ = nullptr;
    }

    if (module_) {
        wasm_runtime_unload(module_);
        module_ = nullptr;
    }

    if (wasm_module_buf_) {
        heap_caps_free(wasm_module_buf_);
        wasm_module_buf_ = nullptr;
    }

    wasi_args_.clear();
    wasi_argv_.clear();
}

void WasmController::Shutdown()
{
    UnloadModule();

    if (runtime_initialized_) {
        wasm_runtime_destroy();
        runtime_initialized_ = false;
    }

    if (wamr_heap_) {
        heap_caps_free(wamr_heap_);
        wamr_heap_ = nullptr;
    }
}

bool WasmController::lookup_exports()
{
    exports_.contract_version = wasm_runtime_lookup_function(inst_, pp_contract::kExportContractVersion);
    exports_.init = wasm_runtime_lookup_function(inst_, pp_contract::kExportInit);
    exports_.tick = wasm_runtime_lookup_function(inst_, pp_contract::kExportTick);
    exports_.alloc = wasm_runtime_lookup_function(inst_, pp_contract::kExportAlloc);
    exports_.free = wasm_runtime_lookup_function(inst_, pp_contract::kExportFree);

    exports_.on_gesture = wasm_runtime_lookup_function(inst_, pp_contract::kExportOnGesture);
    exports_.on_http_request = wasm_runtime_lookup_function(inst_, pp_contract::kExportOnHttpRequest);
    exports_.on_wifi_event = wasm_runtime_lookup_function(inst_, pp_contract::kExportOnWifiEvent);
    exports_.shutdown = wasm_runtime_lookup_function(inst_, pp_contract::kExportShutdown);

    if (!exports_.contract_version || !exports_.init || !exports_.alloc || !exports_.free) {
        ESP_LOGE(kTag, "Missing required exports (contract/init/tick/alloc/free)");
        return false;
    }

    return true;
}

bool WasmController::verify_contract()
{
    uint32_t argv[1] = { 0 };
    if (!call_wasm(exports_.contract_version, 0, argv, pp_contract::kExportContractVersion)) {
        return false;
    }

    const int32_t version = (int32_t)argv[0];
    if (version != pp_contract::kContractVersion) {
        ESP_LOGE(kTag, "Contract version mismatch: got=%" PRId32 " expected=%" PRId32, version,
            pp_contract::kContractVersion);
        return false;
    }

    return true;
}

bool WasmController::call_wasm(wasm_function_inst_t func, uint32_t argc, uint32_t *argv, const char *name)
{
    if (!exec_env_ || !inst_ || !func) {
        return false;
    }

    if (!wasm_runtime_call_wasm(exec_env_, func, argc, argv)) {
        const char *exception = wasm_runtime_get_exception(inst_);
        ESP_LOGE(kTag, "WASM call failed (%s): %s", name ? name : "(unknown)",
            exception ? exception : "(no exception)");
        if (exception) {
            devserver::notify_uploaded_crashed(exception);
        }
        disable_dispatch("exception");
        return false;
    }

    return true;
}

void WasmController::disable_dispatch(const char *reason)
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
        ESP_LOGW(kTag, "pp_shutdown failed: %s", exception ? exception : "(no exception)");
        return false;
    }
    return true;
}

void WasmController::set_wasi_args_from_string(const char *args)
{
    wasi_args_.clear();
    wasi_argv_.clear();

    if (!args || args[0] == '\0') {
        return;
    }

    const char *p = args;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ' ') {
            p++;
        }
        wasi_args_.emplace_back(start, (size_t)(p - start));
    }

    for (const std::string &s : wasi_args_) {
        wasi_argv_.push_back(s.c_str());
    }
}

bool WasmController::CallInit(int32_t api_version, int64_t api_features, int32_t screen_w, int32_t screen_h,
    int32_t args_ptr, int32_t args_len)
{
    if (!dispatch_enabled_ || !exports_.init) {
        return false;
    }

    uint32_t argv[7];
    argv[0] = (uint32_t)api_version;
    memcpy(&argv[1], &api_features, sizeof(api_features));
    argv[3] = (uint32_t)screen_w;
    argv[4] = (uint32_t)screen_h;
    argv[5] = (uint32_t)args_ptr;
    argv[6] = (uint32_t)args_len;

    if (!call_wasm(exports_.init, 7, argv, pp_contract::kExportInit)) {
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
    return call_wasm(exports_.tick, 1, argv, pp_contract::kExportTick);
}

bool WasmController::CallOnGesture(int32_t kind, int32_t x, int32_t y, int32_t dx, int32_t dy,
    int32_t duration_ms, int32_t now_ms, int32_t flags)
{
    if (!dispatch_enabled_ || !exports_.on_gesture) {
        return false;
    }
    uint32_t argv[8] = { (uint32_t)kind, (uint32_t)x, (uint32_t)y, (uint32_t)dx, (uint32_t)dy,
        (uint32_t)duration_ms, (uint32_t)now_ms, (uint32_t)flags };
    return call_wasm(exports_.on_gesture, 8, argv, pp_contract::kExportOnGesture);
}

bool WasmController::CallOnHttpRequest(int32_t req_id, int32_t method, int32_t uri_ptr, int32_t uri_len,
    int32_t body_ptr, int32_t body_len, int32_t content_len, int32_t now_ms, int32_t flags)
{
    if (!dispatch_enabled_ || !exports_.on_http_request) {
        return false;
    }
    uint32_t argv[9] = { (uint32_t)req_id, (uint32_t)method, (uint32_t)uri_ptr, (uint32_t)uri_len,
        (uint32_t)body_ptr, (uint32_t)body_len, (uint32_t)content_len, (uint32_t)now_ms, (uint32_t)flags };
    return call_wasm(exports_.on_http_request, 9, argv, pp_contract::kExportOnHttpRequest);
}

bool WasmController::CallOnWifiEvent(int32_t kind, int32_t now_ms, int32_t arg0, int32_t arg1)
{
    if (!dispatch_enabled_ || !exports_.on_wifi_event) {
        return false;
    }

    uint32_t argv[4] = { (uint32_t)kind, (uint32_t)now_ms, (uint32_t)arg0, (uint32_t)arg1 };
    return call_wasm(exports_.on_wifi_event, 4, argv, pp_contract::kExportOnWifiEvent);
}

int32_t WasmController::CallAlloc(int32_t len)
{
    if (!dispatch_enabled_ || !exports_.alloc) {
        return 0;
    }

    uint32_t argv[1] = { (uint32_t)len };
    if (!call_wasm(exports_.alloc, 1, argv, pp_contract::kExportAlloc)) {
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
    call_wasm(exports_.free, 2, argv, pp_contract::kExportFree);
}

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
