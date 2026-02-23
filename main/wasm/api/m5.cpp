#include <inttypes.h>
#include <stdint.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "m5papers3_display.h"
#include "services/settings_service.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_m5";
constexpr const char *kWasmLogTag = "wasm";

// Keep in sync with M5GFX's lgfx::boards::board_t numbering:
// - M5GFX/src/lgfx/boards.hpp: board_M5PaperS3 == 19
constexpr int32_t kBoardM5PaperS3 = 19;

const char *driver_to_string(PaperDisplayDriver driver)
{
    switch (driver) {
        case PaperDisplayDriver::lgfx:
            return "lgfx";
        case PaperDisplayDriver::fastepd:
            return "fastepd";
        default:
            return "unknown";
    }
}

// Initializes the M5Paper display.
//
// Driver selection is read from `/sdcard/portal/config.json`:
//   { "display": { "driver": "fastepd" | "lgfx" } }
//
// If not configured, defaults to `fastepd`.
//
// Returns kWasmOk on success, or kWasmErrInternal if display initialization fails.
int32_t begin(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    PaperDisplayDriver driver = PaperDisplayDriver::fastepd;
    bool configured = false;
    const esp_err_t err = settings_service::get_display_driver(&driver, &configured);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "begin: get_display_driver failed err=0x%x, using default driver=%s",
                 (unsigned)err, driver_to_string(driver));
    }

    ESP_LOGI(kTag, "begin: driver=%s (configured=%d)", driver_to_string(driver), configured ? 1 : 0);
    if (!paper_display_ensure_init(driver)) {
        ESP_LOGE(kTag, "begin: display initialization failed");
        wasm_api_set_last_error(kWasmErrInternal, "begin: display init failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

// Delays execution for the specified number of milliseconds.
// Args:
//   ms: Delay duration in milliseconds. Must be non-negative.
// Returns kWasmOk on success, or kWasmErrInvalidArgument if ms is negative.
int32_t delayMs(wasm_exec_env_t exec_env, int32_t ms)
{
    (void)exec_env;
    if (ms < 0) {
        ESP_LOGE(kTag, "delayMs: invalid delay ms=%" PRId32 ", must be non-negative", ms);
        wasm_api_set_last_error(kWasmErrInvalidArgument, "delayMs: ms < 0");
        return kWasmErrInvalidArgument;
    }
    if (ms == 0) {
        taskYIELD();
        return kWasmOk;
    }
    vTaskDelay(pdMS_TO_TICKS((uint32_t)ms));
    return kWasmOk;
}

// Returns the number of milliseconds since boot.
// Note: Wraps around approximately every 24.8 days due to int32_t overflow.
int32_t millis(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)(esp_timer_get_time() / 1000);
}

// Returns the number of microseconds since boot.
int64_t micros(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int64_t)(uint64_t)esp_timer_get_time();
}

// Returns the board type identifier.
// Returns kBoardM5PaperS3 (19) as defined in M5GFX's lgfx::boards::board_t.
int32_t board(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return kBoardM5PaperS3;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_m5_native_symbols[] = {
    REG_NATIVE_FUNC(begin, "()i"),
    REG_NATIVE_FUNC(delayMs, "(i)i"),
    REG_NATIVE_FUNC(millis, "()i"),
    REG_NATIVE_FUNC(micros, "()I"),
    REG_NATIVE_FUNC(board, "()i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_m5(void)
{
    const uint32_t count = sizeof(g_m5_native_symbols) / sizeof(g_m5_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5", g_m5_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5 natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_m5: wasm_runtime_register_natives failed");
    }
    return ok;
}
