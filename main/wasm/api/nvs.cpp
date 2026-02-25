#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_nvs";
constexpr int kMaxNvsHandles = 8;
constexpr int kMaxNvsIterators = 8;

nvs_handle_t g_nvs_handles[kMaxNvsHandles] = {};
nvs_iterator_t g_nvs_iterators[kMaxNvsIterators] = {};

#pragma pack(push, 1)
struct WasmNvsEntryInfo {
    char namespace_name[16];
    char key[16];
    uint32_t type;
};

struct WasmNvsStats {
    uint32_t used_entries;
    uint32_t free_entries;
    uint32_t available_entries;
    uint32_t total_entries;
    uint32_t namespace_count;
};
#pragma pack(pop)

static_assert(sizeof(WasmNvsEntryInfo) == 36, "WasmNvsEntryInfo size mismatch");
static_assert(sizeof(WasmNvsStats) == 20, "WasmNvsStats size mismatch");

bool validate_non_empty(const char *value, const char *context)
{
    if (!value || value[0] == '\0') {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }
    return true;
}

bool validate_out_buffer(const void *out_ptr, int32_t out_len, size_t needed, const char *context)
{
    if (!out_ptr) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }
    if (out_len < 0 || (size_t)out_len < needed) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }
    return true;
}

void set_handle_error(const char *context)
{
    char message[96] = {};
    if (context) {
        snprintf(message, sizeof(message), "%s: bad handle", context);
        wasm_api_set_last_error(kWasmErrInvalidArgument, message);
        return;
    }
    wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs: bad handle");
}

int alloc_nvs_handle(nvs_handle_t handle)
{
    for (int i = 0; i < kMaxNvsHandles; i++) {
        if (g_nvs_handles[i] == 0) {
            g_nvs_handles[i] = handle;
            return i + 1;
        }
    }
    return 0;
}

nvs_handle_t get_nvs_handle(int32_t handle)
{
    if (handle <= 0) {
        return 0;
    }
    const int idx = handle - 1;
    if (idx < 0 || idx >= kMaxNvsHandles) {
        return 0;
    }
    return g_nvs_handles[idx];
}

int32_t free_nvs_handle(int32_t handle)
{
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_close: invalid handle");
        return kWasmErrInvalidArgument;
    }
    const int idx = handle - 1;
    if (idx < 0 || idx >= kMaxNvsHandles || g_nvs_handles[idx] == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_close: bad handle");
        return kWasmErrInvalidArgument;
    }
    g_nvs_handles[idx] = 0;
    return kWasmOk;
}

int alloc_iterator(nvs_iterator_t it)
{
    for (int i = 0; i < kMaxNvsIterators; i++) {
        if (g_nvs_iterators[i] == nullptr) {
            g_nvs_iterators[i] = it;
            return i + 1;
        }
    }
    return 0;
}

nvs_iterator_t get_iterator(int32_t handle)
{
    if (handle <= 0) {
        return nullptr;
    }
    const int idx = handle - 1;
    if (idx < 0 || idx >= kMaxNvsIterators) {
        return nullptr;
    }
    return g_nvs_iterators[idx];
}

void free_iterator_slot(int32_t handle)
{
    if (handle <= 0) {
        return;
    }
    const int idx = handle - 1;
    if (idx < 0 || idx >= kMaxNvsIterators) {
        return;
    }
    g_nvs_iterators[idx] = nullptr;
}

int32_t map_nvs_error(esp_err_t err, const char *context)
{
    if (err == ESP_OK) {
        return kWasmOk;
    }

    int32_t code = kWasmErrInternal;
    switch (err) {
        case ESP_ERR_INVALID_ARG:
        case ESP_ERR_NVS_INVALID_HANDLE:
        case ESP_ERR_NVS_INVALID_NAME:
        case ESP_ERR_NVS_INVALID_LENGTH:
        case ESP_ERR_NVS_VALUE_TOO_LONG:
            code = kWasmErrInvalidArgument;
            break;
        case ESP_ERR_NVS_NOT_INITIALIZED:
        case ESP_ERR_NVS_READ_ONLY:
            code = kWasmErrNotReady;
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            code = kWasmErrNotFound;
            break;
        default:
            code = kWasmErrInternal;
            break;
    }

    char message[96] = {};
    const char *err_name = esp_err_to_name(err);
    if (context && err_name) {
        snprintf(message, sizeof(message), "%s: %s", context, err_name);
        wasm_api_set_last_error(code, message);
    } else if (context) {
        wasm_api_set_last_error(code, context);
    } else {
        wasm_api_set_last_error(code, "nvs: error");
    }
    return code;
}

const char *normalize_partition(const char *part_name)
{
    if (!part_name || part_name[0] == '\0') {
        return NVS_DEFAULT_PART_NAME;
    }
    return part_name;
}

const char *normalize_namespace_filter(const char *namespace_name)
{
    if (!namespace_name || namespace_name[0] == '\0') {
        return nullptr;
    }
    return namespace_name;
}

int32_t nvsOpen(wasm_exec_env_t exec_env, const char *namespace_name, int32_t mode)
{
    (void)exec_env;
    if (!validate_non_empty(namespace_name, "nvs_open: namespace is empty")) {
        return kWasmErrInvalidArgument;
    }

    nvs_open_mode_t open_mode = NVS_READONLY;
    if (mode == 0) {
        open_mode = NVS_READONLY;
    } else if (mode == 1) {
        open_mode = NVS_READWRITE;
    } else {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_open: invalid open_mode");
        return kWasmErrInvalidArgument;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = ::nvs_open(namespace_name, open_mode, &handle);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_open");
    }

    const int slot = alloc_nvs_handle(handle);
    if (slot == 0) {
        ::nvs_close(handle);
        wasm_api_set_last_error(kWasmErrInternal, "nvs_open: too many open namespaces");
        return kWasmErrInternal;
    }
    return slot;
}

int32_t nvsClose(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_close: bad handle");
        return kWasmErrInvalidArgument;
    }
    ::nvs_close(nvs_handle);
    return free_nvs_handle(handle);
}

template <typename T>
int32_t nvs_set_number(wasm_exec_env_t exec_env, int32_t handle, const char *key, T value,
    esp_err_t (*setter)(nvs_handle_t, const char *, T), const char *context)
{
    (void)exec_env;
    char key_context[96] = {};
    if (context) {
        snprintf(key_context, sizeof(key_context), "%s: key is empty", context);
    } else {
        snprintf(key_context, sizeof(key_context), "nvs_set: key is empty");
    }
    if (!validate_non_empty(key, key_context)) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        set_handle_error(context);
        return kWasmErrInvalidArgument;
    }
    esp_err_t err = setter(nvs_handle, key, value);
    if (err != ESP_OK) {
        return map_nvs_error(err, context);
    }
    return kWasmOk;
}

template <typename T>
int32_t nvs_get_number(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len,
    esp_err_t (*getter)(nvs_handle_t, const char *, T *), const char *context)
{
    (void)exec_env;
    char key_context[96] = {};
    if (context) {
        snprintf(key_context, sizeof(key_context), "%s: key is empty", context);
    } else {
        snprintf(key_context, sizeof(key_context), "nvs_get: key is empty");
    }
    if (!validate_non_empty(key, key_context)) {
        return kWasmErrInvalidArgument;
    }
    char out_context[96] = {};
    if (context) {
        snprintf(out_context, sizeof(out_context), "%s: out invalid", context);
    } else {
        snprintf(out_context, sizeof(out_context), "nvs_get: out invalid");
    }
    if (!validate_out_buffer(out_ptr, out_len, sizeof(T), out_context)) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        set_handle_error(context);
        return kWasmErrInvalidArgument;
    }

    T value = 0;
    esp_err_t err = getter(nvs_handle, key, &value);
    if (err != ESP_OK) {
        return map_nvs_error(err, context);
    }
    memcpy(out_ptr, &value, sizeof(T));
    return (int32_t)sizeof(T);
}

int32_t nvsSetI8(wasm_exec_env_t exec_env, int32_t handle, const char *key, int32_t value)
{
    if (value < INT8_MIN || value > INT8_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_i8: value out of range");
        return kWasmErrInvalidArgument;
    }
    return nvs_set_number(exec_env, handle, key, (int8_t)value, ::nvs_set_i8, "nvs_set_i8");
}

int32_t nvsSetU8(wasm_exec_env_t exec_env, int32_t handle, const char *key, int32_t value)
{
    if (value < 0 || value > UINT8_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_u8: value out of range");
        return kWasmErrInvalidArgument;
    }
    return nvs_set_number(exec_env, handle, key, (uint8_t)value, ::nvs_set_u8, "nvs_set_u8");
}

int32_t nvsSetI16(wasm_exec_env_t exec_env, int32_t handle, const char *key, int32_t value)
{
    if (value < INT16_MIN || value > INT16_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_i16: value out of range");
        return kWasmErrInvalidArgument;
    }
    return nvs_set_number(exec_env, handle, key, (int16_t)value, ::nvs_set_i16, "nvs_set_i16");
}

int32_t nvsSetU16(wasm_exec_env_t exec_env, int32_t handle, const char *key, int32_t value)
{
    if (value < 0 || value > UINT16_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_u16: value out of range");
        return kWasmErrInvalidArgument;
    }
    return nvs_set_number(exec_env, handle, key, (uint16_t)value, ::nvs_set_u16, "nvs_set_u16");
}

int32_t nvsSetI32(wasm_exec_env_t exec_env, int32_t handle, const char *key, int32_t value)
{
    return nvs_set_number(exec_env, handle, key, value, ::nvs_set_i32, "nvs_set_i32");
}

int32_t nvsSetU32(wasm_exec_env_t exec_env, int32_t handle, const char *key, int32_t value)
{
    if (value < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_u32: value out of range");
        return kWasmErrInvalidArgument;
    }
    return nvs_set_number(exec_env, handle, key, (uint32_t)value, ::nvs_set_u32, "nvs_set_u32");
}

int32_t nvsSetI64(wasm_exec_env_t exec_env, int32_t handle, const char *key, int64_t value)
{
    return nvs_set_number(exec_env, handle, key, value, ::nvs_set_i64, "nvs_set_i64");
}

int32_t nvsSetU64(wasm_exec_env_t exec_env, int32_t handle, const char *key, int64_t value)
{
    if (value < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_u64: value out of range");
        return kWasmErrInvalidArgument;
    }
    return nvs_set_number(exec_env, handle, key, (uint64_t)value, ::nvs_set_u64, "nvs_set_u64");
}

int32_t nvsSetStr(wasm_exec_env_t exec_env, int32_t handle, const char *key, const char *value)
{
    (void)exec_env;
    if (!validate_non_empty(key, "nvs_set_str: key is empty")) {
        return kWasmErrInvalidArgument;
    }
    if (!value) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_str: value is null");
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_str: bad handle");
        return kWasmErrInvalidArgument;
    }

    esp_err_t err = ::nvs_set_str(nvs_handle, key, value);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_set_str");
    }
    return kWasmOk;
}

int32_t nvsSetBlob(wasm_exec_env_t exec_env, int32_t handle, const char *key, const uint8_t *value, int32_t len)
{
    (void)exec_env;
    if (!validate_non_empty(key, "nvs_set_blob: key is empty")) {
        return kWasmErrInvalidArgument;
    }
    if (len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_blob: len < 0");
        return kWasmErrInvalidArgument;
    }
    if (!value && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_blob: value is null");
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_set_blob: bad handle");
        return kWasmErrInvalidArgument;
    }

    esp_err_t err = ::nvs_set_blob(nvs_handle, key, value, (size_t)len);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_set_blob");
    }
    return kWasmOk;
}

int32_t nvsGetI8(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<int8_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_i8, "nvs_get_i8");
}

int32_t nvsGetU8(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<uint8_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_u8, "nvs_get_u8");
}

int32_t nvsGetI16(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<int16_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_i16, "nvs_get_i16");
}

int32_t nvsGetU16(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<uint16_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_u16, "nvs_get_u16");
}

int32_t nvsGetI32(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<int32_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_i32, "nvs_get_i32");
}

int32_t nvsGetU32(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<uint32_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_u32, "nvs_get_u32");
}

int32_t nvsGetI64(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<int64_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_i64, "nvs_get_i64");
}

int32_t nvsGetU64(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    return nvs_get_number<uint64_t>(exec_env, handle, key, out_ptr, out_len, ::nvs_get_u64, "nvs_get_u64");
}

int32_t nvsGetStr(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!validate_non_empty(key, "nvs_get_str: key is empty")) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_get_str: bad handle");
        return kWasmErrInvalidArgument;
    }

    size_t length = 0;
    char *out_value = (char *)out_ptr;
    if (out_ptr) {
        if (out_len < 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_get_str: out_len < 0");
            return kWasmErrInvalidArgument;
        }
        length = (size_t)out_len;
    }

    esp_err_t err = ::nvs_get_str(nvs_handle, key, out_ptr ? out_value : nullptr, &length);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_get_str");
    }
    if (length > (size_t)INT32_MAX) {
        wasm_api_set_last_error(kWasmErrInternal, "nvs_get_str: length overflow");
        return kWasmErrInternal;
    }
    return (int32_t)length;
}

int32_t nvsGetBlob(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!validate_non_empty(key, "nvs_get_blob: key is empty")) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_get_blob: bad handle");
        return kWasmErrInvalidArgument;
    }

    size_t length = 0;
    if (out_ptr) {
        if (out_len < 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_get_blob: out_len < 0");
            return kWasmErrInvalidArgument;
        }
        length = (size_t)out_len;
    }

    esp_err_t err = ::nvs_get_blob(nvs_handle, key, out_ptr ? out_ptr : nullptr, &length);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_get_blob");
    }
    if (length > (size_t)INT32_MAX) {
        wasm_api_set_last_error(kWasmErrInternal, "nvs_get_blob: length overflow");
        return kWasmErrInternal;
    }
    return (int32_t)length;
}

int32_t nvsFindKey(wasm_exec_env_t exec_env, int32_t handle, const char *key, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!validate_non_empty(key, "nvs_find_key: key is empty")) {
        return kWasmErrInvalidArgument;
    }
    if (!validate_out_buffer(out_ptr, out_len, sizeof(uint32_t), "nvs_find_key: out invalid")) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_find_key: bad handle");
        return kWasmErrInvalidArgument;
    }

    nvs_type_t type = NVS_TYPE_ANY;
    esp_err_t err = ::nvs_find_key(nvs_handle, key, &type);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_find_key");
    }
    const uint32_t type_code = (uint32_t)type;
    memcpy(out_ptr, &type_code, sizeof(type_code));
    return (int32_t)sizeof(type_code);
}

int32_t nvsEraseKey(wasm_exec_env_t exec_env, int32_t handle, const char *key)
{
    (void)exec_env;
    if (!validate_non_empty(key, "nvs_erase_key: key is empty")) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_erase_key: bad handle");
        return kWasmErrInvalidArgument;
    }

    esp_err_t err = ::nvs_erase_key(nvs_handle, key);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_erase_key");
    }
    return kWasmOk;
}

int32_t nvsEraseAll(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_erase_all: bad handle");
        return kWasmErrInvalidArgument;
    }

    esp_err_t err = ::nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_erase_all");
    }
    return kWasmOk;
}

int32_t nvsCommit(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_commit: bad handle");
        return kWasmErrInvalidArgument;
    }

    esp_err_t err = ::nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_commit");
    }
    return kWasmOk;
}

int32_t nvsGetStats(wasm_exec_env_t exec_env, const char *part_name, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!validate_out_buffer(out_ptr, out_len, sizeof(WasmNvsStats), "nvs_get_stats: out invalid")) {
        return kWasmErrInvalidArgument;
    }

    const char *partition = (part_name && part_name[0] != '\0') ? part_name : nullptr;
    nvs_stats_t stats = {};
    esp_err_t err = ::nvs_get_stats(partition, &stats);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_get_stats");
    }

    if (stats.used_entries > UINT32_MAX || stats.free_entries > UINT32_MAX || stats.available_entries > UINT32_MAX
        || stats.total_entries > UINT32_MAX || stats.namespace_count > UINT32_MAX) {
        wasm_api_set_last_error(kWasmErrInternal, "nvs_get_stats: value overflow");
        return kWasmErrInternal;
    }

    WasmNvsStats out = {
        (uint32_t)stats.used_entries,
        (uint32_t)stats.free_entries,
        (uint32_t)stats.available_entries,
        (uint32_t)stats.total_entries,
        (uint32_t)stats.namespace_count,
    };
    memcpy(out_ptr, &out, sizeof(out));
    return (int32_t)sizeof(out);
}

int32_t nvsGetUsedEntryCount(wasm_exec_env_t exec_env, int32_t handle, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!validate_out_buffer(out_ptr, out_len, sizeof(uint32_t), "nvs_get_used_entry_count: out invalid")) {
        return kWasmErrInvalidArgument;
    }
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_get_used_entry_count: bad handle");
        return kWasmErrInvalidArgument;
    }

    size_t used_entries = 0;
    esp_err_t err = ::nvs_get_used_entry_count(nvs_handle, &used_entries);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_get_used_entry_count");
    }
    if (used_entries > UINT32_MAX) {
        wasm_api_set_last_error(kWasmErrInternal, "nvs_get_used_entry_count: value overflow");
        return kWasmErrInternal;
    }

    const uint32_t used = (uint32_t)used_entries;
    memcpy(out_ptr, &used, sizeof(used));
    return (int32_t)sizeof(used);
}

int32_t nvsEntryFind(wasm_exec_env_t exec_env, const char *part_name, const char *namespace_name, int32_t type)
{
    (void)exec_env;
    const char *partition = normalize_partition(part_name);
    const char *ns_filter = normalize_namespace_filter(namespace_name);

    nvs_iterator_t it = nullptr;
    esp_err_t err = ::nvs_entry_find(partition, ns_filter, (nvs_type_t)type, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_entry_find");
    }

    const int slot = alloc_iterator(it);
    if (slot == 0) {
        ::nvs_release_iterator(it);
        wasm_api_set_last_error(kWasmErrInternal, "nvs_entry_find: too many iterators");
        return kWasmErrInternal;
    }
    return slot;
}

int32_t nvsEntryFindInHandle(wasm_exec_env_t exec_env, int32_t handle, int32_t type)
{
    (void)exec_env;
    nvs_handle_t nvs_handle = get_nvs_handle(handle);
    if (nvs_handle == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_entry_find_in_handle: bad handle");
        return kWasmErrInvalidArgument;
    }

    nvs_iterator_t it = nullptr;
    esp_err_t err = ::nvs_entry_find_in_handle(nvs_handle, (nvs_type_t)type, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_entry_find_in_handle");
    }

    const int slot = alloc_iterator(it);
    if (slot == 0) {
        ::nvs_release_iterator(it);
        wasm_api_set_last_error(kWasmErrInternal, "nvs_entry_find_in_handle: too many iterators");
        return kWasmErrInternal;
    }
    return slot;
}

int32_t nvsEntryNext(wasm_exec_env_t exec_env, int32_t iterator_handle)
{
    (void)exec_env;
    nvs_iterator_t it = get_iterator(iterator_handle);
    if (!it) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_entry_next: bad iterator");
        return kWasmErrInvalidArgument;
    }

    nvs_iterator_t it_next = it;
    esp_err_t err = ::nvs_entry_next(&it_next);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ::nvs_release_iterator(it_next);
        free_iterator_slot(iterator_handle);
        return 0;
    }
    if (err != ESP_OK) {
        if (!it_next) {
            free_iterator_slot(iterator_handle);
        }
        return map_nvs_error(err, "nvs_entry_next");
    }

    g_nvs_iterators[iterator_handle - 1] = it_next;
    return 1;
}

int32_t nvsEntryInfo(wasm_exec_env_t exec_env, int32_t iterator_handle, uint8_t *out_ptr, int32_t out_len)
{
    (void)exec_env;
    if (!validate_out_buffer(out_ptr, out_len, sizeof(WasmNvsEntryInfo), "nvs_entry_info: out invalid")) {
        return kWasmErrInvalidArgument;
    }
    nvs_iterator_t it = get_iterator(iterator_handle);
    if (!it) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_entry_info: bad iterator");
        return kWasmErrInvalidArgument;
    }

    nvs_entry_info_t info = {};
    esp_err_t err = ::nvs_entry_info(it, &info);
    if (err != ESP_OK) {
        return map_nvs_error(err, "nvs_entry_info");
    }

    WasmNvsEntryInfo out = {};
    strncpy(out.namespace_name, info.namespace_name, sizeof(out.namespace_name) - 1);
    out.namespace_name[sizeof(out.namespace_name) - 1] = '\0';
    strncpy(out.key, info.key, sizeof(out.key) - 1);
    out.key[sizeof(out.key) - 1] = '\0';
    out.type = (uint32_t)info.type;

    memcpy(out_ptr, &out, sizeof(out));
    return (int32_t)sizeof(out);
}

int32_t nvsReleaseIterator(wasm_exec_env_t exec_env, int32_t iterator_handle)
{
    (void)exec_env;
    nvs_iterator_t it = get_iterator(iterator_handle);
    if (!it) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "nvs_release_iterator: bad iterator");
        return kWasmErrInvalidArgument;
    }
    ::nvs_release_iterator(it);
    free_iterator_slot(iterator_handle);
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_nvs_native_symbols[] = {
    REG_NATIVE_FUNC(nvsOpen, "($i)i"),
    REG_NATIVE_FUNC(nvsClose, "(i)i"),

    REG_NATIVE_FUNC(nvsSetI8, "(i$i)i"),
    REG_NATIVE_FUNC(nvsSetU8, "(i$i)i"),
    REG_NATIVE_FUNC(nvsSetI16, "(i$i)i"),
    REG_NATIVE_FUNC(nvsSetU16, "(i$i)i"),
    REG_NATIVE_FUNC(nvsSetI32, "(i$i)i"),
    REG_NATIVE_FUNC(nvsSetU32, "(i$i)i"),
    REG_NATIVE_FUNC(nvsSetI64, "(i$I)i"),
    REG_NATIVE_FUNC(nvsSetU64, "(i$I)i"),
    REG_NATIVE_FUNC(nvsSetStr, "(i$$)i"),
    REG_NATIVE_FUNC(nvsSetBlob, "(i$*i)i"),

    REG_NATIVE_FUNC(nvsGetI8, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetU8, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetI16, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetU16, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetI32, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetU32, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetI64, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetU64, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetStr, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsGetBlob, "(i$*i)i"),

    REG_NATIVE_FUNC(nvsFindKey, "(i$*i)i"),
    REG_NATIVE_FUNC(nvsEraseKey, "(i$)i"),
    REG_NATIVE_FUNC(nvsEraseAll, "(i)i"),
    REG_NATIVE_FUNC(nvsCommit, "(i)i"),

    REG_NATIVE_FUNC(nvsGetStats, "($*i)i"),
    REG_NATIVE_FUNC(nvsGetUsedEntryCount, "(i*i)i"),

    REG_NATIVE_FUNC(nvsEntryFind, "($$i)i"),
    REG_NATIVE_FUNC(nvsEntryFindInHandle, "(ii)i"),
    REG_NATIVE_FUNC(nvsEntryNext, "(i)i"),
    REG_NATIVE_FUNC(nvsEntryInfo, "(i*i)i"),
    REG_NATIVE_FUNC(nvsReleaseIterator, "(i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_nvs(void)
{
    const uint32_t count = sizeof(g_nvs_native_symbols) / sizeof(g_nvs_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_nvs", g_nvs_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_nvs natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_nvs: wasm_runtime_register_natives failed");
    }
    return ok;
}
