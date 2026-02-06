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
constexpr size_t kMaxXtgBytes = 1024 * 1024;

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

bool canonicalize_color_depth(int32_t depth_raw, lgfx::color_depth_t *out_depth, uint32_t *out_bits,
    bool *out_requires_palette, uint32_t *out_palette_entries)
{
    if (!out_depth || !out_bits || !out_requires_palette || !out_palette_entries) {
        wasm_api_set_last_error(kWasmErrInternal, "canonicalize_color_depth: null out pointer");
        return false;
    }

    const uint16_t raw_u16 = (uint16_t)depth_raw;
    const uint32_t bits = (uint32_t)(raw_u16 & (uint16_t)lgfx::color_depth_t::bit_mask);
    const bool has_palette = (raw_u16 & (uint16_t)lgfx::color_depth_t::has_palette) != 0;
    const bool nonswapped = (raw_u16 & (uint16_t)lgfx::color_depth_t::nonswapped) != 0;
    const bool alternate = (raw_u16 & (uint16_t)lgfx::color_depth_t::alternate) != 0;

    lgfx::color_depth_t depth = lgfx::color_depth_t::rgb565_2Byte;
    uint32_t palette_entries = 0;

    switch (bits) {
        case 1:
            depth = has_palette ? lgfx::color_depth_t::palette_1bit : lgfx::color_depth_t::grayscale_1bit;
            palette_entries = 2;
            break;
        case 2:
            depth = has_palette ? lgfx::color_depth_t::palette_2bit : lgfx::color_depth_t::grayscale_2bit;
            palette_entries = 4;
            break;
        case 4:
            depth = has_palette ? lgfx::color_depth_t::palette_4bit : lgfx::color_depth_t::grayscale_4bit;
            palette_entries = 16;
            break;
        case 8:
            if (has_palette) {
                depth = lgfx::color_depth_t::palette_8bit;
                palette_entries = 256;
            } else {
                depth = alternate ? lgfx::color_depth_t::grayscale_8bit : lgfx::color_depth_t::rgb332_1Byte;
            }
            break;
        case 16:
            depth = nonswapped ? lgfx::color_depth_t::rgb565_nonswapped : lgfx::color_depth_t::rgb565_2Byte;
            break;
        case 24:
            if (alternate) {
                depth = nonswapped ? lgfx::color_depth_t::rgb666_nonswapped : lgfx::color_depth_t::rgb666_3Byte;
            } else {
                depth = nonswapped ? lgfx::color_depth_t::rgb888_nonswapped : lgfx::color_depth_t::rgb888_3Byte;
            }
            break;
        case 32:
            depth = nonswapped ? lgfx::color_depth_t::argb8888_nonswapped : lgfx::color_depth_t::argb8888_4Byte;
            break;
        default:
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: invalid color depth bit count");
            return false;
    }

    *out_depth = depth;
    *out_bits = bits;
    *out_palette_entries = palette_entries;
    *out_requires_palette = (palette_entries != 0) || (bits < 8);
    return true;
}

bool compute_expected_image_len(int32_t w, int32_t h, uint32_t bits, size_t *out_expected_len)
{
    if (!out_expected_len) {
        wasm_api_set_last_error(kWasmErrInternal, "compute_expected_image_len: out_expected_len is null");
        return false;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: negative size");
        return false;
    }

    const uint64_t pixels = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h;
    if (pixels > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: size overflow");
        return false;
    }

    uint64_t expected = 0;
    if (bits == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: bits is zero");
        return false;
    }
    if (bits < 8) {
        expected = (pixels * (uint64_t)bits + 7u) / 8u;
    } else {
        expected = pixels * ((uint64_t)bits / 8u);
    }

    if (expected > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: expected_len overflow");
        return false;
    }

    *out_expected_len = (size_t)expected;
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

int32_t push_image(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data_ptr,
    size_t data_len, int32_t depth_raw, const uint8_t *palette_ptr, size_t palette_len)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "push_image: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    lgfx::color_depth_t depth = lgfx::color_depth_t::rgb565_2Byte;
    uint32_t bits = 0;
    bool requires_palette = false;
    uint32_t palette_entries = 0;
    if (!canonicalize_color_depth(depth_raw, &depth, &bits, &requires_palette, &palette_entries)) {
        return kWasmErrInvalidArgument;
    }

    size_t expected_len = 0;
    if (!compute_expected_image_len(w, h, bits, &expected_len)) {
        return kWasmErrInvalidArgument;
    }

    if ((!data_ptr && expected_len != 0) || data_len != expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: data ptr/len mismatch");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return kWasmOk;
    }

    if (bits == 16 && ((uintptr_t)data_ptr & 1u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: data_ptr must be 2-byte aligned for 16bpp");
        return kWasmErrInvalidArgument;
    }
    if (bits == 32 && ((uintptr_t)data_ptr & 3u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: data_ptr must be 4-byte aligned for 32bpp");
        return kWasmErrInvalidArgument;
    }

    // Palette is only used for indexed (<8bpp) and palette_* modes. For other depths, ignore palette args.
    const lgfx::rgb888_t *palette_rgb888 = nullptr;
    if (requires_palette) {
        if (!palette_ptr) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_ptr is null (palette required)");
            return kWasmErrInvalidArgument;
        }
        if ((palette_len & 3u) != 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_len must be multiple of 4 bytes");
            return kWasmErrInvalidArgument;
        }
        if (((uintptr_t)palette_ptr & 3u) != 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_ptr must be 4-byte aligned");
            return kWasmErrInvalidArgument;
        }
        const size_t expected_palette_len = (size_t)palette_entries * 4u;
        if (palette_len != expected_palette_len) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_len mismatch");
            return kWasmErrInvalidArgument;
        }

        // Palette entries are passed from WASM as u32 values in 0x00RRGGBB form (little-endian bytes BB GG RR 00),
        // which matches lgfx::rgb888_t memory layout (b, g, r [+ padding]).
        palette_rgb888 = (const lgfx::rgb888_t *)palette_ptr;
    }

    display->pushImage(x, y, w, h, (const void *)data_ptr, depth, palette_rgb888);
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

int32_t draw_xtg_centered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxXtgBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = drawXtg(*display, ptr, len);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_xtg_centered: decode failed");
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
    REG_NATIVE_FUNC(push_image, "(iiii*~i*~)i"),
    REG_NATIVE_FUNC(push_image_gray8, "(iiii*~)i"),
    REG_NATIVE_FUNC(read_rect_rgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(draw_png, "(*~ii)i"),
    REG_NATIVE_FUNC(draw_xth_centered, "(*~)i"),
    REG_NATIVE_FUNC(draw_xtg_centered, "(*~)i"),
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
