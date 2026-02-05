#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "m5papers3_display.h"
#include "other/xteink_image_utils.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_display_images";
constexpr size_t kMaxPngBytes = 1024 * 1024;
constexpr size_t kMaxJpgBytes = 1024 * 1024;
constexpr size_t kMaxXthBytes = 1024 * 1024;

LGFX_M5PaperS3 *get_display_or_set_error(void)
{
    if (!paper_display_ensure_init()) {
        wasm_api_set_last_error(kWasmErrNotReady, "display not ready (init failed)");
        return nullptr;
    }
    return &paper_display();
}

bool validate_display_rect(const LGFX_M5PaperS3 &display, int32_t x, int32_t y, int32_t w, int32_t h,
    const char *context)
{
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }

    const int32_t max_w = (int32_t)display.width();
    const int32_t max_h = (int32_t)display.height();
    const int64_t x2 = (int64_t)x + (int64_t)w;
    const int64_t y2 = (int64_t)y + (int64_t)h;
    if (x2 > max_w || y2 > max_h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }

    return true;
}

int32_t push_image_rgb565(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *ptr,
    size_t len)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "push_image_rgb565: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    const int64_t expected_len64 = (int64_t)w * (int64_t)h * 2;
    if (expected_len64 < 0 || expected_len64 > (int64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_rgb565: size overflow");
        return kWasmErrInvalidArgument;
    }
    const size_t expected_len = (size_t)expected_len64;

    if ((!ptr && expected_len != 0) || len != expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_rgb565: ptr/len mismatch");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return kWasmOk;
    }
    if (((uintptr_t)ptr & 1u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_rgb565: ptr must be 2-byte aligned");
        return kWasmErrInvalidArgument;
    }

    display->pushImage(x, y, w, h, (const lgfx::rgb565_t *)ptr);
    return kWasmOk;
}

int32_t push_image_gray8(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *ptr,
    size_t len)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "push_image_gray8: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    const int64_t expected_len64 = (int64_t)w * (int64_t)h;
    if (expected_len64 < 0 || expected_len64 > (int64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_gray8: size overflow");
        return kWasmErrInvalidArgument;
    }
    const size_t expected_len = (size_t)expected_len64;

    if ((!ptr && expected_len != 0) || len != expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_gray8: ptr/len mismatch");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return kWasmOk;
    }

    const uint32_t white = lgfx::color888(255, 255, 255);
    const uint32_t black = lgfx::color888(0, 0, 0);
    display->pushGrayscaleImage(x, y, w, h, ptr, lgfx::color_depth_t::grayscale_8bit, white, black);
    return kWasmOk;
}

int32_t read_rect_rgb565(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out,
    size_t out_len)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "read_rect_rgb565: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    const int64_t expected_len64 = (int64_t)w * (int64_t)h * 2;
    if (expected_len64 < 0 || expected_len64 > (int64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: size overflow");
        return kWasmErrInvalidArgument;
    }
    if (expected_len64 > (int64_t)INT32_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: output too large");
        return kWasmErrInvalidArgument;
    }

    const size_t expected_len = (size_t)expected_len64;
    if (!out && expected_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: out_len too small");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return 0;
    }
    if (((uintptr_t)out & 1u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: out must be 2-byte aligned");
        return kWasmErrInvalidArgument;
    }

    display->readRect(x, y, w, h, (lgfx::rgb565_t *)out);
    return (int32_t)expected_len;
}

int32_t draw_png(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: negative coordinates");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxPngBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = display->drawPng(ptr, len, x, y);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t draw_xth_centered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxXthBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = drawXth(*display, ptr, len);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_xth_centered: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t draw_jpg_fit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0 || max_w == 0 || max_h == 0) {
        return kWasmOk;
    }
    if (len > kMaxJpgBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = display->drawJpg(ptr, len, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg_fit: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t draw_png_fit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0 || max_w == 0 || max_h == 0) {
        return kWasmOk;
    }
    if (len > kMaxPngBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = display->drawPng(ptr, len, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png_fit: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t draw_jpg_file(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w, int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_file: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_file: path is null");
        return kWasmErrInvalidArgument;
    }
    if (max_w == 0 || max_h == 0) {
        return kWasmOk;
    }

    const bool ok = display->drawJpgFile(path, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg_file: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t draw_png_file(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w, int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_file: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_file: path is null");
        return kWasmErrInvalidArgument;
    }
    if (max_w == 0 || max_h == 0) {
        return kWasmOk;
    }

    const bool ok = display->drawPngFile(path, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png_file: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, (void *)func_name, signature, NULL }

static NativeSymbol g_display_images_native_symbols[] = {
    REG_NATIVE_FUNC(push_image_rgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(push_image_gray8, "(iiii*~)i"),
    REG_NATIVE_FUNC(read_rect_rgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(draw_png, "(*~ii)i"),
    REG_NATIVE_FUNC(draw_xth_centered, "(*~)i"),
    REG_NATIVE_FUNC(draw_jpg_fit, "(*~iiii)i"),
    REG_NATIVE_FUNC(draw_png_fit, "(*~iiii)i"),
    REG_NATIVE_FUNC(draw_jpg_file, "(*iiii)i"),
    REG_NATIVE_FUNC(draw_png_file, "(*iiii)i"),
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
