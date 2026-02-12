#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "../api.h"
#include "display.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_display_images";

Display *get_display_or_set_error(void)
{
    auto *display = Display::current();
    if (!display) {
        wasm_api_set_last_error(kWasmErrNotReady, "display not ready");
    }
    return display;
}

int32_t pushImageRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->pushImageRgb565(exec_env, x, y, w, h, ptr, len);
}

int32_t pushImage(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *data_ptr,
    size_t data_len,
    int32_t depth_raw,
    const uint8_t *palette_ptr,
    size_t palette_len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->pushImage(exec_env, x, y, w, h, data_ptr, data_len, depth_raw, palette_ptr, palette_len);
}

int32_t pushImageGray8(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->pushImageGray8(exec_env, x, y, w, h, ptr, len);
}

int32_t readRectRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    uint8_t *out,
    size_t out_len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->readRectRgb565(exec_env, x, y, w, h, out, out_len);
}

int32_t drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawPng(exec_env, ptr, len, x, y);
}

int32_t drawXthCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawXthCentered(exec_env, ptr, len);
}

int32_t drawXtgCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawXtgCentered(exec_env, ptr, len);
}

int32_t drawJpgFit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawJpgFit(exec_env, ptr, len, x, y, max_w, max_h);
}

int32_t drawPngFit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawPngFit(exec_env, ptr, len, x, y, max_w, max_h);
}

int32_t drawJpgFile(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w, int32_t max_h)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawJpgFile(exec_env, path, x, y, max_w, max_h);
}

int32_t drawPngFile(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w, int32_t max_h)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawPngFile(exec_env, path, x, y, max_w, max_h);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_display_images_native_symbols[] = {
    REG_NATIVE_FUNC(pushImageRgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(pushImage, "(iiii*~i*~)i"),
    REG_NATIVE_FUNC(pushImageGray8, "(iiii*~)i"),
    REG_NATIVE_FUNC(readRectRgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(drawPng, "(*~ii)i"),
    REG_NATIVE_FUNC(drawXthCentered, "(*~)i"),
    REG_NATIVE_FUNC(drawXtgCentered, "(*~)i"),
    REG_NATIVE_FUNC(drawJpgFit, "(*~iiii)i"),
    REG_NATIVE_FUNC(drawPngFit, "(*~iiii)i"),
    REG_NATIVE_FUNC(drawJpgFile, "(*iiii)i"),
    REG_NATIVE_FUNC(drawPngFile, "(*iiii)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_display_images(void)
{
    const uint32_t count = sizeof(g_display_images_native_symbols) / sizeof(g_display_images_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_display", g_display_images_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_display image natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display_images: wasm_runtime_register_natives failed");
    }
    return ok;
}

