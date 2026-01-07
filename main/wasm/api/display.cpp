#include <inttypes.h>
#include <stdint.h>
#include "display.h"
#include "display_none.h"
#include "display_lgfx.h"
#include "display_fastepd.h"
#include "esp_log.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"

std::unique_ptr<Display> Display::_current = std::make_unique<DisplayNone>();

namespace {

constexpr const char *kTag = "wasm_api_display";

const char *driver_to_string(PaperDisplayDriver driver) {
    switch (driver) {
        case PaperDisplayDriver::lgfx:
            return "lgfx";
        case PaperDisplayDriver::fastepd:
            return "fastepd";
        default:
            return "unknown";
    }
}

} // namespace

Display* Display::current() {
    return _current.get();
}

void Display::setCurrent(PaperDisplayDriver driver) {
    const PaperDisplayDriver current_driver = _current->driver();
    if (current_driver != driver) {
        ESP_LOGI(kTag, "setCurrent: switching driver %s -> %s",
                 driver_to_string(current_driver), driver_to_string(driver));
        const int32_t release_rc = _current->release(nullptr);
        if (release_rc != kWasmOk) {
            ESP_LOGW(kTag, "setCurrent: release(%s) failed rc=%" PRId32,
                     driver_to_string(current_driver), release_rc);
        } else {
            ESP_LOGI(kTag, "setCurrent: released driver %s", driver_to_string(current_driver));
        }
        _current.reset();
        switch (driver) {
            case PaperDisplayDriver::lgfx:
                _current = std::make_unique<DisplayLgfx>();
                break;
            case PaperDisplayDriver::fastepd:
                _current = std::make_unique<DisplayFastEpd>();
                break;
            case PaperDisplayDriver::none:
                _current = std::make_unique<DisplayNone>();
                break;
        }
        ESP_LOGI(kTag, "setCurrent: active driver is now %s", driver_to_string(_current->driver()));
    } else {
        ESP_LOGI(kTag, "setCurrent: driver unchanged (%s)", driver_to_string(driver));
    }
}

namespace {

int32_t width(wasm_exec_env_t exec_env)
{
    return Display::current()->width(exec_env);
}

int32_t height(wasm_exec_env_t exec_env)
{
    return Display::current()->height(exec_env);
}

int32_t getRotation(wasm_exec_env_t exec_env)
{
    return Display::current()->getRotation(exec_env);
}

int32_t setRotation(wasm_exec_env_t exec_env, int32_t rot)
{
    return Display::current()->setRotation(exec_env, rot);
}

int32_t clear(wasm_exec_env_t exec_env)
{
    return Display::current()->clear(exec_env);
}

int32_t fillScreen(wasm_exec_env_t exec_env, int32_t rgb888)
{
    return Display::current()->fillScreen(exec_env, rgb888);
}

int32_t display(wasm_exec_env_t exec_env)
{
    return Display::current()->display(exec_env);
}

int32_t displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return Display::current()->displayRect(exec_env, x, y, w, h);
}

int32_t waitDisplay(wasm_exec_env_t exec_env)
{
    return Display::current()->waitDisplay(exec_env);
}

int32_t startWrite(wasm_exec_env_t exec_env)
{
    return Display::current()->startWrite(exec_env);
}

int32_t endWrite(wasm_exec_env_t exec_env)
{
    return Display::current()->endWrite(exec_env);
}

int32_t setBrightness(wasm_exec_env_t exec_env, int32_t v)
{
    return Display::current()->setBrightness(exec_env, v);
}

int32_t getBrightness(wasm_exec_env_t exec_env)
{
    return Display::current()->getBrightness(exec_env);
}

int32_t setEpdMode(wasm_exec_env_t exec_env, int32_t mode)
{
    return Display::current()->setEpdMode(exec_env, mode);
}

int32_t getEpdMode(wasm_exec_env_t exec_env)
{
    return Display::current()->getEpdMode(exec_env);
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
