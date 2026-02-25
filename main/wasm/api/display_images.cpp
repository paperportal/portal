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

extern const uint8_t _binary_icon_battery_png_start[] asm("_binary_icon_battery_png_start");
extern const uint8_t _binary_icon_battery_png_end[] asm("_binary_icon_battery_png_end");
extern const uint8_t _binary_icon_devserver_png_start[] asm("_binary_icon_devserver_png_start");
extern const uint8_t _binary_icon_devserver_png_end[] asm("_binary_icon_devserver_png_end");
extern const uint8_t _binary_icon_softap_png_start[] asm("_binary_icon_softap_png_start");
extern const uint8_t _binary_icon_softap_png_end[] asm("_binary_icon_softap_png_end");
extern const uint8_t _binary_icon_wifi_png_start[] asm("_binary_icon_wifi_png_start");
extern const uint8_t _binary_icon_wifi_png_end[] asm("_binary_icon_wifi_png_end");

int32_t pushImageRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    return Display::current()->pushImageRgb565(exec_env, x, y, w, h, ptr, len);
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
    return Display::current()->pushImage(exec_env, x, y, w, h, data_ptr, data_len, depth_raw, palette_ptr, palette_len);
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
    return Display::current()->pushImageGray8(exec_env, x, y, w, h, ptr, len);
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
    return Display::current()->readRectRgb565(exec_env, x, y, w, h, out, out_len);
}

int32_t drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    return Display::current()->drawPng(exec_env, ptr, len, x, y);
}

int32_t drawIcon(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t icon_raw)
{
    if (icon_raw < 0 || icon_raw >= (int32_t)PaperIcon::count) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawIcon: invalid icon id");
        return kWasmErrInvalidArgument;
    }

    const uint8_t *start = nullptr;
    const uint8_t *end = nullptr;

    switch ((PaperIcon)icon_raw) {
    case PaperIcon::battery:
        start = _binary_icon_battery_png_start;
        end = _binary_icon_battery_png_end;
        break;
    case PaperIcon::devserver:
        start = _binary_icon_devserver_png_start;
        end = _binary_icon_devserver_png_end;
        break;
    case PaperIcon::softap:
        start = _binary_icon_softap_png_start;
        end = _binary_icon_softap_png_end;
        break;
    case PaperIcon::wifi:
        start = _binary_icon_wifi_png_start;
        end = _binary_icon_wifi_png_end;
        break;
    case PaperIcon::count:
        break;
    }

    if (!start || !end || end <= start) {
        wasm_api_set_last_error(kWasmErrInternal, "drawIcon: invalid embedded icon data");
        return kWasmErrInternal;
    }

    return Display::current()->drawPng(exec_env, start, (size_t)(end - start), x, y);
}

int32_t drawXth(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, bool fast)
{
    return Display::current()->drawXth(exec_env, ptr, len, fast);
}

int32_t drawXtg(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, bool fast)
{
    return Display::current()->drawXtg(exec_env, ptr, len, fast);
}

int32_t drawJpgFit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    return Display::current()->drawJpgFit(exec_env, ptr, len, x, y, max_w, max_h);
}

int32_t drawPngFit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    return Display::current()->drawPngFit(exec_env, ptr, len, x, y, max_w, max_h);
}

int32_t drawJpgFile(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w, int32_t max_h)
{
    return Display::current()->drawJpgFile(exec_env, path, x, y, max_w, max_h);
}

int32_t drawPngFile(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w, int32_t max_h)
{
    return Display::current()->drawPngFile(exec_env, path, x, y, max_w, max_h);
}

#define REG_NATIVE_FUNC(funcName, signature)  { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_display_images_native_symbols[] = {
    REG_NATIVE_FUNC(pushImageRgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(pushImage, "(iiii*~i*~)i"),
    REG_NATIVE_FUNC(pushImageGray8, "(iiii*~)i"),
    REG_NATIVE_FUNC(readRectRgb565, "(iiii*~)i"),
    REG_NATIVE_FUNC(drawPng, "(*~ii)i"),
    REG_NATIVE_FUNC(drawIcon, "(iii)i"),
    REG_NATIVE_FUNC(drawXth, "(*~i)i"),
    REG_NATIVE_FUNC(drawXtg, "(*~i)i"),
    REG_NATIVE_FUNC(drawJpgFit, "(*~iiii)i"),
    REG_NATIVE_FUNC(drawPngFit, "(*~iiii)i"),
    REG_NATIVE_FUNC(drawJpgFile, "(*iiii)i"),
    REG_NATIVE_FUNC(drawPngFile, "(*iiii)i"),
};

} // namespace

bool wasm_api_register_display_images(void)
{
    const uint32_t count = sizeof(g_display_images_native_symbols) / sizeof(g_display_images_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_display", g_display_images_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_display image natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display_images: wasm_runtime_register_natives failed");
    }
    return ok;
}
