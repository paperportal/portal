#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "m5papers3_display.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_display_primitives";

LGFX_M5PaperS3 *get_display_or_set_error(void)
{
    if (!paper_display_ensure_init()) {
        wasm_api_set_last_error(kWasmErrNotReady, "display not ready (init failed)");
        return nullptr;
    }
    return &paper_display();
}

uint32_t color_from_rgb888(int32_t rgb888)
{
    const uint32_t raw = (uint32_t)rgb888;
    const uint8_t r = (uint8_t)((raw >> 16) & 0xFF);
    const uint8_t g = (uint8_t)((raw >> 8) & 0xFF);
    const uint8_t b = (uint8_t)(raw & 0xFF);
    return lgfx::color888(r, g, b);
}

int32_t draw_pixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->drawPixel(x, y, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_line(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->drawLine(x0, y0, x1, y1, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_fast_vline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_fast_vline: h < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawFastVLine(x, y, h, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_fast_hline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_fast_hline: w < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawFastHLine(x, y, w, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_rect: w < 0 or h < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawRect(x, y, w, h, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t fill_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fill_rect: w < 0 or h < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillRect(x, y, w, h, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_round_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0 || r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_round_rect: w < 0 or h < 0 or r < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawRoundRect(x, y, w, h, r, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t fill_round_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0 || r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fill_round_rect: w < 0 or h < 0 or r < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillRoundRect(x, y, w, h, r, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_circle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_circle: r < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawCircle(x, y, r, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t fill_circle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fill_circle: r < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillCircle(x, y, r, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t fill_arc(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t r0,
    int32_t r1,
    float angle0,
    float angle1,
    int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (r0 < 0 || r1 < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fill_arc: r0 < 0 or r1 < 0");
        return kWasmErrInvalidArgument;
    }
    if (r1 > r0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fill_arc: r1 > r0");
        return kWasmErrInvalidArgument;
    }
    display->fillArc(x, y, r0, r1, angle0, angle1, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_ellipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rx < 0 || ry < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_ellipse: rx < 0 or ry < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawEllipse(x, y, rx, ry, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t fill_ellipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rx < 0 || ry < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fill_ellipse: rx < 0 or ry < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillEllipse(x, y, rx, ry, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t draw_triangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->drawTriangle(x0, y0, x1, y1, x2, y2, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t fill_triangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->fillTriangle(x0, y0, x1, y1, x2, y2, color_from_rgb888(rgb888));
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, (void *)func_name, signature, NULL }

static NativeSymbol g_display_primitives_native_symbols[] = {
    REG_NATIVE_FUNC(draw_pixel, "(iii)i"),
    REG_NATIVE_FUNC(draw_line, "(iiiii)i"),
    REG_NATIVE_FUNC(draw_fast_vline, "(iiii)i"),
    REG_NATIVE_FUNC(draw_fast_hline, "(iiii)i"),
    REG_NATIVE_FUNC(draw_rect, "(iiiii)i"),
    REG_NATIVE_FUNC(fill_rect, "(iiiii)i"),
    REG_NATIVE_FUNC(draw_round_rect, "(iiiiii)i"),
    REG_NATIVE_FUNC(fill_round_rect, "(iiiiii)i"),
    REG_NATIVE_FUNC(draw_circle, "(iiii)i"),
    REG_NATIVE_FUNC(fill_circle, "(iiii)i"),
    REG_NATIVE_FUNC(fill_arc, "(iiiiffi)i"),
    REG_NATIVE_FUNC(draw_ellipse, "(iiiii)i"),
    REG_NATIVE_FUNC(fill_ellipse, "(iiiii)i"),
    REG_NATIVE_FUNC(draw_triangle, "(iiiiiii)i"),
    REG_NATIVE_FUNC(fill_triangle, "(iiiiiii)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_display_primitives(void)
{
    const uint32_t count =
        sizeof(g_display_primitives_native_symbols) / sizeof(g_display_primitives_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_display", g_display_primitives_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_display primitives natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display_primitives: wasm_runtime_register_natives failed");
    }
    return ok;
}
