#include <inttypes.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "wasm/app_contract.h"
#include "wasm_controller.h"

static constexpr const char *kTag = "wasm_controller";

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

    if (!LookupExports()) {
        return false;
    }

    if (!VerifyContract()) {
        return false;
    }

    dispatch_enabled_ = true;

#if CONFIG_WAMR_ENABLE_MEMORY_PROFILING
    if (esp_log_level_get(kTag) >= ESP_LOG_DEBUG) {
        wasm_runtime_dump_mem_consumption(exec_env_);
    }
#endif

    return true;
}

void WasmController::UnloadModule()
{
    dispatch_enabled_ = false;
    main_called_ = false;

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

bool WasmController::LookupExports()
{
    exports_.contract_version = wasm_runtime_lookup_function(inst_, pp_contract::kExportContractVersion);
    exports_.microtask_step = wasm_runtime_lookup_function(inst_, pp_contract::kExportPortalMicroTaskStep);
    exports_.alloc = wasm_runtime_lookup_function(inst_, pp_contract::kExportAlloc);
    exports_.free = wasm_runtime_lookup_function(inst_, pp_contract::kExportFree);

    exports_.on_gesture = wasm_runtime_lookup_function(inst_, pp_contract::kExportOnGesture);
    exports_.on_http_request = wasm_runtime_lookup_function(inst_, pp_contract::kExportOnHttpRequest);
    exports_.on_wifi_event = wasm_runtime_lookup_function(inst_, pp_contract::kExportOnWifiEvent);
    exports_.shutdown = wasm_runtime_lookup_function(inst_, pp_contract::kExportShutdown);

    if (!exports_.contract_version || !exports_.microtask_step || !exports_.alloc || !exports_.free) {
        ESP_LOGE(kTag, "Missing required exports (contract/microtask/alloc/free)");
        return false;
    }

    return true;
}

bool WasmController::VerifyContract()
{
    uint32_t argv[1] = { 0 };
    if (!CallWasm(exports_.contract_version, 0, argv, pp_contract::kExportContractVersion)) {
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
