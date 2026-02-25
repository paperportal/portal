#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#if __has_include(<esp_adc/adc_cali_scheme.h>)
#include <esp_adc/adc_cali_scheme.h>
#endif
#include <soc/adc_channel.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "m5papers3_display.h"
#include "services/power_service.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_power";

// M5PaperS3 behavior ported from M5Unified:
// - Battery voltage measured via ADC1 on GPIO3, scale ratio 2.0.
// - Charge status on GPIO4, low == charging.
// - USB detect on GPIO5, high == USB connected. (Matches the upstream UserDemo.)
constexpr gpio_num_t kPaperS3ChargeStatusPin = GPIO_NUM_4;
constexpr gpio_num_t kPaperS3UsbDetectPin = GPIO_NUM_5;
constexpr float kPaperS3BatteryAdcRatio = 2.0f;

adc_oneshot_unit_handle_t g_adc_handle = nullptr;
adc_cali_handle_t g_adc_cali = nullptr;

bool g_power_inited = false;

void ensure_charge_status_pin_configured(void)
{
    static bool configured = false;
    if (configured) {
        return;
    }
    configured = true;

    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << static_cast<uint32_t>(kPaperS3ChargeStatusPin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

void ensure_usb_detect_pin_configured(void)
{
    static bool configured = false;
    if (configured) {
        return;
    }
    configured = true;

    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << static_cast<uint32_t>(kPaperS3UsbDetectPin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

int32_t get_battery_adc_raw_mv(void)
{
    // Mirrors M5Unified's _getBatteryAdcRaw() behavior (ESP-IDF v5+ oneshot ADC).
    if (g_adc_handle == nullptr) {
        adc_oneshot_unit_init_cfg_t init_config{};
        init_config.unit_id = ADC_UNIT_1;
        if (adc_oneshot_new_unit(&init_config, &g_adc_handle) != ESP_OK || g_adc_handle == nullptr) {
            return 0;
        }

        adc_oneshot_chan_cfg_t config{};
        config.atten = ADC_ATTEN_DB_12;
        config.bitwidth = ADC_BITWIDTH_12;
        // M5Unified uses ADC1_GPIO3_CHANNEL (ADC1 on GPIO3).
        adc_oneshot_config_channel(g_adc_handle, (adc_channel_t)ADC1_GPIO3_CHANNEL, &config);
    }

    if (g_adc_cali == nullptr) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config{};
        cali_config.unit_id = ADC_UNIT_1;
        cali_config.chan = (adc_channel_t)ADC1_GPIO3_CHANNEL;
        cali_config.atten = ADC_ATTEN_DB_12;
        cali_config.bitwidth = ADC_BITWIDTH_12;
        (void)adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cali_config{};
        cali_config.unit_id = ADC_UNIT_1;
        cali_config.atten = ADC_ATTEN_DB_12;
        cali_config.bitwidth = ADC_BITWIDTH_12;
        (void)adc_cali_create_scheme_line_fitting(&cali_config, &g_adc_cali);
#endif
    }

    int raw = 0;
    (void)adc_oneshot_read(g_adc_handle, (adc_channel_t)ADC1_GPIO3_CHANNEL, &raw);

    if (g_adc_cali != nullptr) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(g_adc_cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }

    // If calibration isn't available, M5Unified returns the raw ADC reading.
    return raw;
}

int16_t get_battery_voltage_mv(void)
{
    return static_cast<int16_t>(get_battery_adc_raw_mv() * kPaperS3BatteryAdcRatio);
}

int32_t get_battery_level_percent(void)
{
    const float mv = static_cast<float>(get_battery_voltage_mv());
    const int level = static_cast<int>((mv - 3300.0f) * 100.0f / static_cast<float>(4150 - 3350));
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

int32_t powerBegin(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    ensure_charge_status_pin_configured();
    ensure_usb_detect_pin_configured();
    g_power_inited = true;
    return kWasmOk;
}

int32_t powerBatteryLevel(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (!g_power_inited) {
        wasm_api_set_last_error(kWasmErrNotReady, "powerBatteryLevel: power not initialized");
        return kWasmErrNotReady;
    }
    return get_battery_level_percent();
}

int32_t powerBatteryVoltageMv(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (!g_power_inited) {
        wasm_api_set_last_error(kWasmErrNotReady, "powerBatteryVoltageMv: power not initialized");
        return kWasmErrNotReady;
    }
    return (int32_t)get_battery_voltage_mv();
}

int32_t powerBatteryCurrentMa(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (!g_power_inited) {
        wasm_api_set_last_error(kWasmErrNotReady, "powerBatteryCurrentMa: power not initialized");
        return kWasmErrNotReady;
    }
    // M5Unified's M5PaperS3 uses pmic_adc and does not provide a battery current reading.
    return 0;
}

int32_t powerVbusVoltageMv(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    wasm_api_set_last_error(kWasmErrInternal, "powerVbusVoltageMv: VBUS not supported on this model");
    return kWasmErrInternal;
}

int32_t powerIsCharging(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (!g_power_inited) {
        wasm_api_set_last_error(kWasmErrNotReady, "powerIsCharging: power not initialized");
        return kWasmErrNotReady;
    }
    ensure_charge_status_pin_configured();
    const int level = gpio_get_level(kPaperS3ChargeStatusPin);
    return (level == 0) ? 1 : 0; // low == charging
}

int32_t powerIsUsbConnected(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (!g_power_inited) {
        wasm_api_set_last_error(kWasmErrNotReady, "powerIsUsbConnected: power not initialized");
        return kWasmErrNotReady;
    }
    ensure_usb_detect_pin_configured();
    const int level = gpio_get_level(kPaperS3UsbDetectPin);
    return (level == 1) ? 1 : 0;
}

int32_t powerSetBatteryCharge(wasm_exec_env_t exec_env, int32_t enable)
{
    (void)exec_env;
    if (!g_power_inited) {
        wasm_api_set_last_error(kWasmErrNotReady, "powerSetBatteryCharge: power not initialized");
        return kWasmErrNotReady;
    }
    // M5Unified doesn't support toggling charge enable for M5PaperS3 (pmic_adc).
    (void)enable;
    return kWasmOk;
}

int32_t powerRestart(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    esp_restart();
    return kWasmOk;
}

int32_t powerLightSleepUs(wasm_exec_env_t exec_env, int64_t us)
{
    (void)exec_env;
    if (us < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "powerLightSleepUs: us < 0");
        return kWasmErrInvalidArgument;
    }
    if (us > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)us);
    } else {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    esp_light_sleep_start();
    return kWasmOk;
}

int32_t powerDeepSleepUs(wasm_exec_env_t exec_env, int64_t us)
{
    (void)exec_env;
    if (us < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "powerDeepSleepUs: us < 0");
        return kWasmErrInvalidArgument;
    }
    if (us > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)us);
    } else {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    esp_deep_sleep_start();
    return kWasmOk;
}

static int32_t power_off_impl(wasm_exec_env_t exec_env, bool show_sleep_image)
{
    (void)exec_env;
    const esp_err_t err = power_service::power_off(show_sleep_image);
    if (err != ESP_OK) {
        wasm_api_set_last_error(kWasmErrInternal, "powerOff: power off failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t powerOff(wasm_exec_env_t exec_env)
{
    return power_off_impl(exec_env, false);
}

int32_t powerOffWithSleepImage(wasm_exec_env_t exec_env, int32_t show_sleep_image)
{
    return power_off_impl(exec_env, show_sleep_image != 0);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_power_native_symbols[] = {
    REG_NATIVE_FUNC(powerBegin, "()i"),
    REG_NATIVE_FUNC(powerBatteryLevel, "()i"),
    REG_NATIVE_FUNC(powerBatteryVoltageMv, "()i"),
    REG_NATIVE_FUNC(powerBatteryCurrentMa, "()i"),
    REG_NATIVE_FUNC(powerVbusVoltageMv, "()i"),
    REG_NATIVE_FUNC(powerIsCharging, "()i"),
    REG_NATIVE_FUNC(powerIsUsbConnected, "()i"),
    REG_NATIVE_FUNC(powerSetBatteryCharge, "(i)i"),
    REG_NATIVE_FUNC(powerRestart, "()i"),
    REG_NATIVE_FUNC(powerLightSleepUs, "(I)i"),
    REG_NATIVE_FUNC(powerDeepSleepUs, "(I)i"),
    REG_NATIVE_FUNC(powerOff, "()i"),
    REG_NATIVE_FUNC(powerOffWithSleepImage, "(i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_power(void)
{
    const uint32_t count = sizeof(g_power_native_symbols) / sizeof(g_power_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_power", g_power_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_power natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_power: wasm_runtime_register_natives failed");
    }
    return ok;
}
