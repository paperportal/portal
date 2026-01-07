#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "m5papers3_display.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_display";

LGFX_M5PaperS3 *get_display_or_set_error(void)
{
    if (!paper_display_ensure_init()) {
        wasm_api_set_last_error(kWasmErrNotReady, "display not ready (init failed)");
        return nullptr;
    }
    return &paper_display();
}

int32_t width(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->width();
}

int32_t height(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->height();
}

int32_t get_rotation(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getRotation();
}

int32_t set_rotation(wasm_exec_env_t exec_env, int32_t rot)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rot < 0 || rot > 3) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "set_rotation: rot out of range (expected 0..3)");
        return kWasmErrInvalidArgument;
    }
    display->setRotation((uint_fast8_t)rot);
    return kWasmOk;
}

int32_t clear(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->clearDisplay();
    return kWasmOk;
}

int32_t fill_screen(wasm_exec_env_t exec_env, int32_t rgb888)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    const uint32_t raw = (uint32_t)rgb888;
    const uint8_t r = (uint8_t)((raw >> 16) & 0xFF);
    const uint8_t g = (uint8_t)((raw >> 8) & 0xFF);
    const uint8_t b = (uint8_t)(raw & 0xFF);
    display->fillScreen(lgfx::color888(r, g, b));
    return kWasmOk;
}

int32_t display(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->display();
    return kWasmOk;
}

int32_t display_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "display_rect: negative argument");
        return kWasmErrInvalidArgument;
    }

    const int32_t max_w = (int32_t)display->width();
    const int32_t max_h = (int32_t)display->height();
    const int64_t x2 = (int64_t)x + (int64_t)w;
    const int64_t y2 = (int64_t)y + (int64_t)h;
    if (x2 > max_w || y2 > max_h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "display_rect: rect out of bounds");
        return kWasmErrInvalidArgument;
    }

    display->display(x, y, w, h);
    return kWasmOk;
}

int32_t wait_display(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->waitDisplay();
    return kWasmOk;
}

int32_t start_write(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->startWrite();
    return kWasmOk;
}

int32_t end_write(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->endWrite();
    return kWasmOk;
}

int32_t set_brightness(wasm_exec_env_t exec_env, int32_t v)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (v < 0 || v > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "set_brightness: v out of range (expected 0..255)");
        return kWasmErrInvalidArgument;
    }
    display->setBrightness((uint8_t)v);
    return kWasmOk;
}

int32_t get_brightness(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getBrightness();
}

int32_t set_epd_mode(wasm_exec_env_t exec_env, int32_t mode)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (mode < 1 || mode > 4) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "set_epd_mode: mode out of range (1..4)");
        return kWasmErrInvalidArgument;
    }
    display->setEpdMode((lgfx::epd_mode_t)mode);
    return kWasmOk;
}

int32_t get_epd_mode(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getEpdMode();
}

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, (void *)func_name, signature, NULL }

static NativeSymbol g_display_native_symbols[] = {
    REG_NATIVE_FUNC(width, "()i"),
    REG_NATIVE_FUNC(height, "()i"),
    REG_NATIVE_FUNC(get_rotation, "()i"),
    REG_NATIVE_FUNC(set_rotation, "(i)i"),
    REG_NATIVE_FUNC(clear, "()i"),
    REG_NATIVE_FUNC(fill_screen, "(i)i"),
    REG_NATIVE_FUNC(display, "()i"),
    REG_NATIVE_FUNC(display_rect, "(iiii)i"),
    REG_NATIVE_FUNC(wait_display, "()i"),
    REG_NATIVE_FUNC(start_write, "()i"),
    REG_NATIVE_FUNC(end_write, "()i"),
    REG_NATIVE_FUNC(set_brightness, "(i)i"),
    REG_NATIVE_FUNC(get_brightness, "()i"),
    REG_NATIVE_FUNC(set_epd_mode, "(i)i"),
    REG_NATIVE_FUNC(get_epd_mode, "()i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_display(void)
{
    const uint32_t count = sizeof(g_display_native_symbols) / sizeof(g_display_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_display", g_display_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_display natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display: wasm_runtime_register_natives failed");
    }
    return ok;
}
