#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "sd_card.h"
#include "wasm_controller.h"

static constexpr const char *kTag = "wasm_controller";

extern "C" const uint8_t _binary_entrypoint_wasm_start[];
extern "C" const uint8_t _binary_entrypoint_wasm_end[];
extern "C" const uint8_t _binary_settings_wasm_start[];
extern "C" const uint8_t _binary_settings_wasm_end[];

bool WasmController::AllocateWasmModuleBuffer(size_t len)
{
    if (len == 0) {
        return false;
    }

    bool psram_ready = esp_psram_is_initialized();
    if (psram_ready) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!wasm_module_buf_) {
        wasm_module_buf_ = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }

    return wasm_module_buf_ != nullptr;
}

bool WasmController::LoadModuleFromOwnedBuffer(size_t len, const char *args, char *error, size_t error_len)
{
    if (!wasm_module_buf_ || len == 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "invalid module buffer");
        }
        return false;
    }

    char local_error[256] = "";
    char *err_buf = (error && error_len > 0) ? error : local_error;
    size_t err_len = (error && error_len > 0) ? error_len : sizeof(local_error);

    module_ = wasm_runtime_load(wasm_module_buf_, len, err_buf, err_len);
    if (!module_) {
        heap_caps_free(wasm_module_buf_);
        wasm_module_buf_ = nullptr;
        return false;
    }

    SetWasiArgsFromString(args);
    wasm_runtime_set_wasi_args(module_,
        nullptr, 0, nullptr, 0, nullptr, 0,
        wasi_argv_.empty() ? nullptr : const_cast<char**>(wasi_argv_.data()), (uint32_t)wasi_argv_.size());

    return true;
}

void WasmController::SetWasiArgsFromString(const char *args)
{
    wasi_args_.clear();
    wasi_argv_.clear();

    // WASI convention: argv[0] is the program name.
    wasi_args_.emplace_back("app");

    if (!args || args[0] == '\0') {
        for (const std::string &s : wasi_args_) {
            wasi_argv_.push_back(s.c_str());
        }
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

bool WasmController::LoadEntrypoint()
{
    if (!runtime_initialized_) {
        ESP_LOGE(kTag, "LoadEntrypoint called before Init");
        return false;
    }

    if (module_) {
        return true;
    }

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

                if (!AllocateWasmModuleBuffer(file_size)) {
                    ESP_LOGE(kTag, "Failed to allocate wasm module buffer (%u bytes)", (unsigned)file_size);
                    fclose(f);
                } else {
                    size_t bytes_read = fread(wasm_module_buf_, 1, file_size, f);
                    fclose(f);

                    if (bytes_read != file_size) {
                        ESP_LOGE(kTag, "Failed to read entrypoint.wasm (read %u of %u bytes)", (unsigned)bytes_read,
                            (unsigned)file_size);
                        heap_caps_free(wasm_module_buf_);
                        wasm_module_buf_ = nullptr;
                    } else {
                        char error_buf[1000] = "";
                        if (LoadModuleFromOwnedBuffer(file_size, nullptr, error_buf, sizeof(error_buf))) {
                            ESP_LOGI(kTag, "Successfully loaded entrypoint.wasm from SD card");
                            return true;
                        }
                        ESP_LOGE(kTag, "Failed to load entrypoint.wasm -- %s", error_buf);
                    }
                }
            }
        } else {
            ESP_LOGI(kTag, "No entrypoint.wasm found at %s, using embedded entrypoint", entrypoint_path);
        }
    } else {
        ESP_LOGI(kTag, "SD card not mounted, using embedded entrypoint");
    }

    return LoadEmbeddedEntrypoint();
}

bool WasmController::LoadEmbeddedEntrypoint(const char *wasi_args)
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

    if (!AllocateWasmModuleBuffer(wasm_module_size)) {
        ESP_LOGE(kTag, "Failed to allocate wasm module buffer (%u bytes)", (unsigned)wasm_module_size);
        return false;
    }

    memcpy(wasm_module_buf_, wasm_module, wasm_module_size);

    char error_buf[1000] = "";
    if (!LoadModuleFromOwnedBuffer(wasm_module_size, wasi_args, error_buf, sizeof(error_buf))) {
        ESP_LOGE(kTag, "Failed to load wasm module -- %s", error_buf);
        return false;
    }

    return true;
}

bool WasmController::LoadEmbeddedSettings(const char *wasi_args)
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

    if (!AllocateWasmModuleBuffer(wasm_module_size)) {
        ESP_LOGE(kTag, "Failed to allocate wasm module buffer (%u bytes)", (unsigned)wasm_module_size);
        return false;
    }

    memcpy(wasm_module_buf_, wasm_module, wasm_module_size);

    char error_buf[1000] = "";
    if (!LoadModuleFromOwnedBuffer(wasm_module_size, wasi_args, error_buf, sizeof(error_buf))) {
        ESP_LOGE(kTag, "Failed to load wasm module -- %s", error_buf);
        return false;
    }

    return true;
}

bool WasmController::LoadFromBytes(const uint8_t *bytes, size_t len, const char *args, char *error, size_t error_len)
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

    if (!AllocateWasmModuleBuffer(len)) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "alloc failed (%u bytes)", (unsigned)len);
        }
        return false;
    }

    memcpy(wasm_module_buf_, bytes, len);

    if (!LoadModuleFromOwnedBuffer(len, args, error, error_len)) {
        return false;
    }

    return true;
}

bool WasmController::LoadFromFile(const char *abs_path, const char *wasi_args, char *error, size_t error_len)
{
    if (!runtime_initialized_) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "LoadFromFile called before Init");
        }
        return false;
    }

    if (!abs_path || abs_path[0] == '\0') {
        if (error && error_len > 0) {
            snprintf(error, error_len, "invalid path");
        }
        return false;
    }

    struct stat st;
    if (stat(abs_path, &st) != 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "stat failed (errno=%d)", errno);
        }
        return false;
    }
    if (!S_ISREG(st.st_mode) || st.st_size <= 0) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "not a regular file");
        }
        return false;
    }

    FILE *f = fopen(abs_path, "rb");
    if (!f) {
        if (error && error_len > 0) {
            snprintf(error, error_len, "open failed (errno=%d)", errno);
        }
        return false;
    }

    UnloadModule();

    const size_t file_size = (size_t)st.st_size;
    if (!AllocateWasmModuleBuffer(file_size)) {
        fclose(f);
        if (error && error_len > 0) {
            snprintf(error, error_len, "alloc failed (%u bytes)", (unsigned)file_size);
        }
        return false;
    }

    const size_t bytes_read = fread(wasm_module_buf_, 1, file_size, f);
    fclose(f);

    if (bytes_read != file_size) {
        heap_caps_free(wasm_module_buf_);
        wasm_module_buf_ = nullptr;
        if (error && error_len > 0) {
            snprintf(error, error_len, "short read (read %u of %u bytes)", (unsigned)bytes_read, (unsigned)file_size);
        }
        return false;
    }

    if (!LoadModuleFromOwnedBuffer(file_size, wasi_args, error, error_len)) {
        return false;
    }

    return true;
}
