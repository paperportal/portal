#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "../api.h"
#include "display.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_display_primitives";

int32_t drawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888)
{
    return Display::current()->drawPixel(exec_env, x, y, rgb888);
}

int32_t drawLine(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t rgb888)
{
    return Display::current()->drawLine(exec_env, x0, y0, x1, y1, rgb888);
}

int32_t drawFastVline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888)
{
    return Display::current()->drawFastVline(exec_env, x, y, h, rgb888);
}

int32_t drawFastHline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888)
{
    return Display::current()->drawFastHline(exec_env, x, y, w, rgb888);
}

int32_t drawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    return Display::current()->drawRect(exec_env, x, y, w, h, rgb888);
}

int32_t fillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    return Display::current()->fillRect(exec_env, x, y, w, h, rgb888);
}

int32_t drawRoundRect(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    int32_t r,
    int32_t rgb888)
{
    return Display::current()->drawRoundRect(exec_env, x, y, w, h, r, rgb888);
}

int32_t fillRoundRect(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    int32_t r,
    int32_t rgb888)
{
    return Display::current()->fillRoundRect(exec_env, x, y, w, h, r, rgb888);
}

int32_t drawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    return Display::current()->drawCircle(exec_env, x, y, r, rgb888);
}

int32_t fillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    return Display::current()->fillCircle(exec_env, x, y, r, rgb888);
}

int32_t fillArc(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t r0,
    int32_t r1,
    float angle0,
    float angle1,
    int32_t rgb888)
{
    return Display::current()->fillArc(exec_env, x, y, r0, r1, angle0, angle1, rgb888);
}

int32_t drawEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    return Display::current()->drawEllipse(exec_env, x, y, rx, ry, rgb888);
}

int32_t fillEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    return Display::current()->fillEllipse(exec_env, x, y, rx, ry, rgb888);
}

int32_t drawTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    return Display::current()->drawTriangle(exec_env, x0, y0, x1, y1, x2, y2, rgb888);
}

int32_t fillTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    return Display::current()->fillTriangle(exec_env, x0, y0, x1, y1, x2, y2, rgb888);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_display_primitives_native_symbols[] = {
    REG_NATIVE_FUNC(drawPixel, "(iii)i"),
    REG_NATIVE_FUNC(drawLine, "(iiiii)i"),
    REG_NATIVE_FUNC(drawFastVline, "(iiii)i"),
    REG_NATIVE_FUNC(drawFastHline, "(iiii)i"),
    REG_NATIVE_FUNC(drawRect, "(iiiii)i"),
    REG_NATIVE_FUNC(fillRect, "(iiiii)i"),
    REG_NATIVE_FUNC(drawRoundRect, "(iiiiii)i"),
    REG_NATIVE_FUNC(fillRoundRect, "(iiiiii)i"),
    REG_NATIVE_FUNC(drawCircle, "(iiii)i"),
    REG_NATIVE_FUNC(fillCircle, "(iiii)i"),
    REG_NATIVE_FUNC(fillArc, "(iiiiffi)i"),
    REG_NATIVE_FUNC(drawEllipse, "(iiiii)i"),
    REG_NATIVE_FUNC(fillEllipse, "(iiiii)i"),
    REG_NATIVE_FUNC(drawTriangle, "(iiiiiii)i"),
    REG_NATIVE_FUNC(fillTriangle, "(iiiiiii)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_display_primitives(void)
{
    const uint32_t count =
        sizeof(g_display_primitives_native_symbols) / sizeof(g_display_primitives_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_display", g_display_primitives_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_display primitives natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display_primitives: wasm_runtime_register_natives failed");
    }
    return ok;
}
