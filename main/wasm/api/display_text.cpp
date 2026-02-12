#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "../api.h"
#include "display.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_display_text";

Display *get_display_or_set_error(void)
{
    auto *display = Display::current();
    if (!display) {
        wasm_api_set_last_error(kWasmErrNotReady, "display not ready");
    }
    return display;
}

int32_t setCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setCursor(exec_env, x, y);
}

int32_t setTextSize(wasm_exec_env_t exec_env, float sx, float sy)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextSize(exec_env, sx, sy);
}

int32_t setTextDatum(wasm_exec_env_t exec_env, int32_t datum)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextDatum(exec_env, datum);
}

int32_t setTextColor(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextColor(exec_env, fg_rgb888, bg_rgb888, use_bg);
}

int32_t setTextWrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextWrap(exec_env, wrap_x, wrap_y);
}

int32_t setTextScroll(wasm_exec_env_t exec_env, int32_t scroll)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextScroll(exec_env, scroll);
}

int32_t setTextFont(wasm_exec_env_t exec_env, int32_t font_id)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextFont(exec_env, font_id);
}

int32_t setTextEncoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->setTextEncoding(exec_env, utf8_enable, cp437_enable);
}

int32_t drawString(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->drawString(exec_env, s, x, y);
}

int32_t textWidth(wasm_exec_env_t exec_env, const char *s)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->textWidth(exec_env, s);
}

int32_t fontHeight(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->fontHeight(exec_env);
}

int32_t vlwRegister(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->vlwRegister(exec_env, ptr, len);
}

int32_t vlwUse(wasm_exec_env_t exec_env, int32_t handle)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->vlwUse(exec_env, handle);
}

int32_t vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->vlwUseSystem(exec_env, font_id);
}

int32_t vlwUnload(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->vlwUnload(exec_env);
}

int32_t vlwClearAll(wasm_exec_env_t exec_env)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return display->vlwClearAll(exec_env);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_display_text_native_symbols[] = {
    REG_NATIVE_FUNC(setCursor, "(ii)i"),
    REG_NATIVE_FUNC(setTextSize, "(ff)i"),
    REG_NATIVE_FUNC(setTextDatum, "(i)i"),
    REG_NATIVE_FUNC(setTextColor, "(iii)i"),
    REG_NATIVE_FUNC(setTextWrap, "(ii)i"),
    REG_NATIVE_FUNC(setTextScroll, "(i)i"),
    REG_NATIVE_FUNC(setTextFont, "(i)i"),
    REG_NATIVE_FUNC(setTextEncoding, "(ii)i"),
    REG_NATIVE_FUNC(drawString, "(*ii)i"),
    REG_NATIVE_FUNC(textWidth, "(*)i"),
    REG_NATIVE_FUNC(fontHeight, "()i"),
    REG_NATIVE_FUNC(vlwRegister, "(*~)i"),
    REG_NATIVE_FUNC(vlwUse, "(i)i"),
    REG_NATIVE_FUNC(vlwUseSystem, "(i)i"),
    REG_NATIVE_FUNC(vlwUnload, "()i"),
    REG_NATIVE_FUNC(vlwClearAll, "()i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_display_text(void)
{
    const uint32_t count = sizeof(g_display_text_native_symbols) / sizeof(g_display_text_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_display", g_display_text_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_display text natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display_text: wasm_runtime_register_natives failed");
    }
    return ok;
}
