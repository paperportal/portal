#include <inttypes.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"

extern "C" bool paperportal_speaker_begin(void);
extern "C" bool paperportal_speaker_tone(float freq_hz, uint32_t duration_ms);

namespace {

constexpr const char *kTag = "wasm_api_hal";
static bool g_ext_started = false;
static const gpio_num_t kExtPins[] = {GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2};

void ext_task(void *arg)
{
    (void)arg;

    (void)paperportal_speaker_begin();

    // Beeper: 5 short beeps
    for (int i = 0; i < 5; i++) {
        (void)paperportal_speaker_tone(4000.0f, 100);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool level = false;
    while (true) {
        for (auto pin : kExtPins) {
            gpio_set_level(pin, level ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        level = !level;
    }
}

int32_t extPortTestStart(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (g_ext_started) {
        return kWasmOk;
    }

    for (auto pin : kExtPins) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
        gpio_set_level(pin, 0);
    }

    (void)paperportal_speaker_begin();

    BaseType_t ok = xTaskCreate(ext_task, "ext_port", 1024 * 4, NULL, 5, NULL);
    if (ok != pdPASS) {
        wasm_api_set_last_error(kWasmErrInternal, "extPortTestStart: task create failed");
        return kWasmErrInternal;
    }

    g_ext_started = true;
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_hal_native_symbols[] = {
    REG_NATIVE_FUNC(extPortTestStart, "()i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_hal(void)
{
    const uint32_t count = sizeof(g_hal_native_symbols) / sizeof(g_hal_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_hal", g_hal_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_hal natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_hal: wasm_runtime_register_natives failed");
    }
    return ok;
}
