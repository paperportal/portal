#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "input/touch_tracker.h"
#include "m5papers3_display.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_touch";

static TouchTracker &g_touch = touch_tracker();

LGFX_M5PaperS3 *get_display_or_set_error(void)
{
    if (!paper_display_ensure_init()) {
        wasm_api_set_last_error(kWasmErrNotReady, "touch not ready (display init failed)");
        return nullptr;
    }
    return &paper_display();
}

int32_t update_touch_or_return_error(void)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    g_touch.update(display, lgfx::millis());
    return kWasmOk;
}

/**
 * @brief Return the current number of tracked touch points.
 *
 * WebAssembly import:
 * - Module: `portal_touch`
 * - Name: `touchGetCount`
 * - Signature: `()i`
 *
 * Notes:
 * - Touch state is updated by `M5.update()`. Callers typically invoke this
 *   after an `m5::update()` on the wasm side to obtain fresh touch data.
 *
 * @param exec_env WAMR execution environment (unused).
 * @return Non-negative touch point count.
 */
int32_t touchGetCount(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = update_touch_or_return_error();
    if (rc != kWasmOk) {
        return rc;
    }
    return (int32_t)g_touch.getCount();
}

/**
 * @brief Fetch a raw touch point and write it into a guest-provided buffer.
 *
 * WebAssembly import:
 * - Module: `portal_touch`
 * - Name: `touchGetRaw`
 * - Signature: `(i*~)i`
 *
 * Buffer format (`TouchPointRaw`, 8 bytes, little-endian):
 * - `int16 x`, `int16 y`          : touch coordinates
 * - `uint16 size`                : touch size/pressure metric (device-specific)
 * - `uint16 id`                  : touch identifier
 *
 * Error handling:
 * - On success, writes exactly 8 bytes and returns `8`.
 * - On failure, returns a `kWasmErr*` code and sets the global last-error
 *   message via `wasm_api_set_last_error()`.
 *
 * @param exec_env WAMR execution environment (unused).
 * @param index Touch point index in `[0, touchGetCount())`.
 * @param out Guest output buffer pointer (may be null only if `out_len == 0`).
 * @param out_len Length of `out` in bytes; must be at least `sizeof(TouchPointRaw)`.
 * @return `8` on success, otherwise a negative `kWasmErr*` value.
 */
int32_t touchGetRaw(wasm_exec_env_t exec_env, int32_t index, uint8_t *out, size_t out_len)
{
    (void)exec_env;
    const int32_t rc = update_touch_or_return_error();
    if (rc != kWasmOk) {
        return rc;
    }
    if (index < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetRaw: index < 0");
        return kWasmErrInvalidArgument;
    }
    const uint8_t count = g_touch.getCount();
    if ((uint32_t)index >= (uint32_t)count) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetRaw: index out of range");
        return kWasmErrInvalidArgument;
    }
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetRaw: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < sizeof(TouchPointRaw)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetRaw: out_len too small");
        return kWasmErrInvalidArgument;
    }

    const lgfx::touch_point_t &tp = g_touch.getTouchPointRaw((size_t)index);
    TouchPointRaw raw = {
        .x = tp.x,
        .y = tp.y,
        .size = tp.size,
        .id = tp.id,
    };
    memcpy(out, &raw, sizeof(raw));
    return (int32_t)sizeof(raw);
}

/**
 * @brief Fetch detailed touch information and write it into a guest buffer.
 *
 * WebAssembly import:
 * - Module: `portal_touch`
 * - Name: `touchGetDetail`
 * - Signature: `(i*~)i`
 *
 * Buffer format (`TouchDetail`, 24 bytes, little-endian):
 * - `int16 x`, `int16 y`          : current coordinates
 * - `uint16 size`, `uint16 id`    : size metric and touch identifier
 * - `int16 prev_x`, `int16 prev_y`: previous coordinates
 * - `int16 base_x`, `int16 base_y`: base coordinates for gestures
 * - `uint32 base_msec`            : base timestamp in milliseconds
 * - `uint8 state`                 : touch state bitfield (M5Unified)
 * - `uint8 click_count`           : click count (M5Unified)
 * - `uint16 _pad`                 : reserved/padding (currently 0)
 *
 * Error handling:
 * - On success, writes exactly 24 bytes and returns `24`.
 * - On failure, returns a `kWasmErr*` code and sets the global last-error
 *   message via `wasm_api_set_last_error()`.
 *
 * @param exec_env WAMR execution environment (unused).
 * @param index Touch point index in `[0, touchGetCount())`.
 * @param out Guest output buffer pointer (may be null only if `out_len == 0`).
 * @param out_len Length of `out` in bytes; must be at least `sizeof(TouchDetail)`.
 * @return `24` on success, otherwise a negative `kWasmErr*` value.
 */
int32_t touchGetDetail(wasm_exec_env_t exec_env, int32_t index, uint8_t *out, size_t out_len)
{
    (void)exec_env;
    const int32_t rc = update_touch_or_return_error();
    if (rc != kWasmOk) {
        return rc;
    }
    if (index < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetDetail: index < 0");
        return kWasmErrInvalidArgument;
    }
    const uint8_t count = g_touch.getCount();
    if ((uint32_t)index >= (uint32_t)count) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetDetail: index out of range");
        return kWasmErrInvalidArgument;
    }
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetDetail: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < sizeof(TouchDetail)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchGetDetail: out_len too small");
        return kWasmErrInvalidArgument;
    }

    const TouchDetail &td = g_touch.getDetail((size_t)index);
    TouchDetail det = {
        .x = td.x,
        .y = td.y,
        .size = td.size,
        .id = td.id,
        .prev_x = td.prev_x,
        .prev_y = td.prev_y,
        .base_x = td.base_x,
        .base_y = td.base_y,
        .base_msec = td.base_msec,
        .state = (uint8_t)td.state,
        .click_count = td.click_count,
        ._pad = 0,
    };

    memcpy(out, &det, sizeof(det));
    return (int32_t)sizeof(det);
}

/**
 * @brief Configure the press-and-hold gesture threshold.
 *
 * WebAssembly import:
 * - Module: `portal_touch`
 * - Name: `touchSetHoldThresh`
 * - Signature: `(i)i`
 *
 * @param exec_env WAMR execution environment (unused).
 * @param ms Threshold in milliseconds. Must be within `0..=65535`.
 * @return `kWasmOk` on success, otherwise a negative `kWasmErr*` value.
 */
int32_t touchSetHoldThresh(wasm_exec_env_t exec_env, int32_t ms)
{
    (void)exec_env;
    if (ms < 0 || ms > (int32_t)UINT16_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchSetHoldThresh: ms out of range (0..65535)");
        return kWasmErrInvalidArgument;
    }
    g_touch.setHoldThresh((uint16_t)ms);
    return kWasmOk;
}

/**
 * @brief Configure the flick gesture distance threshold.
 *
 * WebAssembly import:
 * - Module: `portal_touch`
 * - Name: `touchSetFlickThresh`
 * - Signature: `(i)i`
 *
 * @param exec_env WAMR execution environment (unused).
 * @param distance Threshold distance in pixels. Must be within `0..=65535`.
 * @return `kWasmOk` on success, otherwise a negative `kWasmErr*` value.
 */
int32_t touchSetFlickThresh(wasm_exec_env_t exec_env, int32_t distance)
{
    (void)exec_env;
    if (distance < 0 || distance > (int32_t)UINT16_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "touchSetFlickThresh: distance out of range (0..65535)");
        return kWasmErrInvalidArgument;
    }
    g_touch.setFlickThresh((uint16_t)distance);
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, nullptr }

static NativeSymbol g_touch_native_symbols[] = {
    REG_NATIVE_FUNC(touchGetCount, "()i"),
    REG_NATIVE_FUNC(touchGetRaw, "(i*~)i"),
    REG_NATIVE_FUNC(touchGetDetail, "(i*~)i"),
    REG_NATIVE_FUNC(touchSetHoldThresh, "(i)i"),
    REG_NATIVE_FUNC(touchSetFlickThresh, "(i)i"),
};
/* clang-format on */

} // namespace

/**
 * @brief Register `portal_touch` host functions with WAMR.
 *
 * This registers all touch-related native symbols defined in this translation
 * unit (e.g. `touchGetCount`, `touchGetRaw`, ... ) under the module name
 * `portal_touch` so wasm modules can import them.
 *
 * On failure, this sets the global last-error to `kWasmErrInternal`.
 *
 * @return true on success, false on failure.
 */
bool wasm_api_register_touch(void)
{
    const uint32_t count = sizeof(g_touch_native_symbols) / sizeof(g_touch_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_touch", g_touch_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_touch natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_touch: wasm_runtime_register_natives failed");
    }
    return ok;
}
