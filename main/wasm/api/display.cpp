#include <inttypes.h>
#include <stdint.h>
#include "display.h"
#include "esp_log.h"
#include "wasm_export.h"

#include "m5papers3_display.h"

#include "../api.h"
#include "errors.h"

std::unique_ptr<Display> Display::_current = nullptr;

Display* Display::current() {
    return _current.get();
}

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

int32_t getRotation(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getRotation();
}

int32_t setRotation(wasm_exec_env_t exec_env, int32_t rot)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rot < 0 || rot > 3) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setRotation: rot out of range (expected 0..3)");
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

int32_t fillScreen(wasm_exec_env_t exec_env, int32_t rgb888)
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

int32_t displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "displayRect: negative argument");
        return kWasmErrInvalidArgument;
    }

    const int32_t max_w = (int32_t)display->width();
    const int32_t max_h = (int32_t)display->height();
    const int64_t x2 = (int64_t)x + (int64_t)w;
    const int64_t y2 = (int64_t)y + (int64_t)h;
    if (x2 > max_w || y2 > max_h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "displayRect: rect out of bounds");
        return kWasmErrInvalidArgument;
    }

    display->display(x, y, w, h);
    return kWasmOk;
}

int32_t waitDisplay(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->waitDisplay();
    return kWasmOk;
}

int32_t startWrite(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->startWrite();
    return kWasmOk;
}

int32_t endWrite(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->endWrite();
    return kWasmOk;
}

int32_t setBrightness(wasm_exec_env_t exec_env, int32_t v)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (v < 0 || v > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setBrightness: v out of range (expected 0..255)");
        return kWasmErrInvalidArgument;
    }
    display->setBrightness((uint8_t)v);
    return kWasmOk;
}

int32_t getBrightness(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getBrightness();
}

int32_t setEpdMode(wasm_exec_env_t exec_env, int32_t mode)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (mode < 1 || mode > 4) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setEpdMode: mode out of range (1..4)");
        return kWasmErrInvalidArgument;
    }
    display->setEpdMode((lgfx::epd_mode_t)mode);
    return kWasmOk;
}

int32_t getEpdMode(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getEpdMode();
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_display_native_symbols[] = {
    REG_NATIVE_FUNC(width, "()i"),
    REG_NATIVE_FUNC(height, "()i"),
    REG_NATIVE_FUNC(getRotation, "()i"),
    REG_NATIVE_FUNC(setRotation, "(i)i"),
    REG_NATIVE_FUNC(clear, "()i"),
    REG_NATIVE_FUNC(fillScreen, "(i)i"),
    REG_NATIVE_FUNC(display, "()i"),
    REG_NATIVE_FUNC(displayRect, "(iiii)i"),
    REG_NATIVE_FUNC(waitDisplay, "()i"),
    REG_NATIVE_FUNC(startWrite, "()i"),
    REG_NATIVE_FUNC(endWrite, "()i"),
    REG_NATIVE_FUNC(setBrightness, "(i)i"),
    REG_NATIVE_FUNC(getBrightness, "()i"),
    REG_NATIVE_FUNC(setEpdMode, "(i)i"),
    REG_NATIVE_FUNC(getEpdMode, "()i"),
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
