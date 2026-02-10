#include <inttypes.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_speaker";

// M5PaperS3 buzzer pin (matches M5Unified's board_M5PaperS3 speaker config).
static constexpr gpio_num_t kSpeakerPin = GPIO_NUM_21;

static constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
static constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;

static constexpr uint32_t kDutyResolutionBits = 10;
static constexpr ledc_timer_bit_t kDutyResolution = LEDC_TIMER_10_BIT;
static constexpr uint32_t kMaxDuty = (1U << kDutyResolutionBits) - 1U;
static constexpr uint32_t kMaxHalfDuty = kMaxDuty / 2U;

// Some ESP-IDF versions have issues changing LEDC frequency after configuring a timer with
// LEDC_AUTO_CLK. Prefer an explicit clock source when available.
#if defined(LEDC_USE_APB_CLK)
static constexpr ledc_clk_cfg_t kLedcClkCfg = LEDC_USE_APB_CLK;
#elif defined(LEDC_USE_XTAL_CLK)
static constexpr ledc_clk_cfg_t kLedcClkCfg = LEDC_USE_XTAL_CLK;
#else
static constexpr ledc_clk_cfg_t kLedcClkCfg = LEDC_AUTO_CLK;
#endif

static bool g_speaker_running = false;
static uint32_t g_configured_freq_hz = 0;
static uint32_t g_active_freq_hz = 0;
static uint8_t g_volume = 64; // matches M5Unified default master volume

static TimerHandle_t g_tone_stop_timer = nullptr;
static uint32_t g_tone_end_tick = 0; // 0 == no scheduled stop (infinite or idle)

uint32_t duty_from_volume(uint8_t volume)
{
    return (uint32_t)volume * kMaxHalfDuty / 255U;
}

void speaker_stop_hw(void)
{
    g_tone_end_tick = 0;
    g_active_freq_hz = 0;

    if (!g_speaker_running) {
        return;
    }

    // Stop PWM output; keep the LEDC peripheral configured for the next tone.
    ledc_stop(kLedcMode, kLedcChannel, 0);
}

void tone_stop_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    const uint32_t end_tick = g_tone_end_tick;
    if (end_tick == 0) {
        return;
    }

    const uint32_t now = (uint32_t)xTaskGetTickCount();
    if ((int32_t)(now - end_tick) < 0) {
        // Stale/early callback; a newer tone is active with a later stop time.
        return;
    }

    speaker_stop_hw();
}

bool speaker_backend_is_enabled(void)
{
    return kSpeakerPin >= 0;
}

bool speaker_backend_begin(void)
{
    if (g_speaker_running) {
        return true;
    }
    if (!speaker_backend_is_enabled()) {
        return false;
    }

    if (!g_tone_stop_timer) {
        g_tone_stop_timer = xTimerCreate("pp_spk_stop", 1, pdFALSE, nullptr, tone_stop_timer_cb);
        if (!g_tone_stop_timer) {
            ESP_LOGE(kTag, "Failed to create tone stop timer");
            return false;
        }
    }

    // Configure PWM for a buzzer-style tone output.
    ledc_timer_config_t timer_cfg{};
    timer_cfg.speed_mode = kLedcMode;
    timer_cfg.timer_num = kLedcTimer;
    timer_cfg.duty_resolution = kDutyResolution;
    timer_cfg.freq_hz = 4000; // safe default; updated per-tone
    timer_cfg.clk_cfg = kLedcClkCfg;
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return false;
    }

    ledc_channel_config_t ch_cfg{};
    ch_cfg.speed_mode = kLedcMode;
    ch_cfg.channel = kLedcChannel;
    ch_cfg.timer_sel = kLedcTimer;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.gpio_num = (int)kSpeakerPin;
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return false;
    }

    g_speaker_running = true;
    g_configured_freq_hz = timer_cfg.freq_hz;
    g_active_freq_hz = 0;
    speaker_stop_hw();
    return true;
}

void speaker_backend_end(void)
{
    if (!g_speaker_running) {
        return;
    }

    if (g_tone_stop_timer) {
        (void)xTimerStop(g_tone_stop_timer, 0);
    }
    speaker_stop_hw();
    g_speaker_running = false;
    gpio_reset_pin(kSpeakerPin);
}

bool speaker_backend_is_running(void)
{
    return g_speaker_running;
}

void speaker_backend_set_volume(uint8_t volume)
{
    g_volume = volume;
    if (!g_speaker_running || g_active_freq_hz == 0) {
        return;
    }

    const uint32_t duty = duty_from_volume(g_volume);
    (void)ledc_set_duty(kLedcMode, kLedcChannel, duty);
    (void)ledc_update_duty(kLedcMode, kLedcChannel);
}

uint8_t speaker_backend_get_volume(void)
{
    return g_volume;
}

void speaker_backend_stop(void)
{
    if (g_tone_stop_timer) {
        (void)xTimerStop(g_tone_stop_timer, 0);
    }
    speaker_stop_hw();
}

bool speaker_backend_tone(float freq_hz, uint32_t duration_ms)
{
    // Match M5Unified semantics: tone is best-effort and should not fail hard if the
    // hardware isn't present.
    if (!speaker_backend_begin()) {
        return true;
    }

    if (freq_hz <= 0.0f) {
        speaker_backend_stop();
        return true;
    }

    uint32_t freq_i = (uint32_t)(freq_hz + 0.5f);
    if (freq_i < 1) {
        freq_i = 1;
    } else if (freq_i > 20000) {
        freq_i = 20000;
    }

    uint32_t actual = freq_i;
    if (freq_i != g_configured_freq_hz) {
        actual = ledc_set_freq(kLedcMode, kLedcTimer, freq_i);
        if (actual == 0) {
            // Fallback: reconfigure the timer, which tends to be more reliable across IDF versions.
            ledc_timer_config_t timer_cfg{};
            timer_cfg.speed_mode = kLedcMode;
            timer_cfg.timer_num = kLedcTimer;
            timer_cfg.duty_resolution = kDutyResolution;
            timer_cfg.freq_hz = freq_i;
            timer_cfg.clk_cfg = kLedcClkCfg;
            esp_err_t err = ledc_timer_config(&timer_cfg);
            if (err != ESP_OK) {
                static uint32_t s_last_warn_tick = 0;
                const uint32_t now = (uint32_t)xTaskGetTickCount();
                // Throttle to ~1 log/sec to avoid spam in tight loops.
                if ((int32_t)(now - s_last_warn_tick) >= (int32_t)pdMS_TO_TICKS(1000)) {
                    s_last_warn_tick = now;
                    ESP_LOGW(kTag, "Failed to set speaker freq to %lu Hz (ledc_set_freq + timer_config)", (unsigned long)freq_i);
                }
                return true;
            }
            actual = freq_i;
        }
        g_configured_freq_hz = actual;
    }

    const uint32_t duty = duty_from_volume(g_volume);
    (void)ledc_set_duty(kLedcMode, kLedcChannel, duty);
    (void)ledc_update_duty(kLedcMode, kLedcChannel);
    g_active_freq_hz = actual;

    if (duration_ms == UINT32_MAX) {
        // Infinite tone: cancel any pending stop.
        if (g_tone_stop_timer) {
            (void)xTimerStop(g_tone_stop_timer, 0);
        }
        g_tone_end_tick = 0;
        return true;
    }

    TickType_t ticks = pdMS_TO_TICKS(duration_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    g_tone_end_tick = (uint32_t)xTaskGetTickCount() + (uint32_t)ticks;

    if (g_tone_stop_timer) {
        (void)xTimerStop(g_tone_stop_timer, 0);
        (void)xTimerChangePeriod(g_tone_stop_timer, ticks, 0);
        (void)xTimerStart(g_tone_stop_timer, 0);
    }

    return true;
}

// Beeper task state
static bool g_beeper_running = false;
static bool g_beeper_should_stop = false;
static TaskHandle_t g_beeper_task_handle = nullptr;

// Beeper pattern parameters
static float g_beeper_freq = 4000.0f;
static int32_t g_beeper_count = 4;
static int32_t g_beeper_duration = 100;
static int32_t g_beeper_gap = 100;
static int32_t g_beeper_pause = 1000;

static void beeper_task(void* arg)
{
    (void)arg;

    while (!g_beeper_should_stop) {
        // Play the beep pattern
        for (int i = 0; i < g_beeper_count; i++) {
            if (g_beeper_should_stop) {
                break;
            }
            (void)speaker_backend_tone(g_beeper_freq, (uint32_t)g_beeper_duration);
            vTaskDelay(pdMS_TO_TICKS(g_beeper_gap));
        }

        if (g_beeper_should_stop) {
            break;
        }

        // Pause before next pattern
        int32_t pause_remaining = g_beeper_pause;
        while (pause_remaining > 0 && !g_beeper_should_stop) {
            int32_t delay_ms = (pause_remaining > 100) ? 100 : pause_remaining;
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            pause_remaining -= delay_ms;
        }
    }

    // Ensure speaker is stopped
    speaker_backend_stop();

    g_beeper_task_handle = nullptr;
    g_beeper_running = false;
    g_beeper_should_stop = false;

    vTaskDelete(NULL);
}

int32_t speakerBegin(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    bool ok = speaker_backend_begin();
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "speakerBegin: speaker init failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t speakerEnd(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    speaker_backend_end();
    return kWasmOk;
}

int32_t speakerIsEnabled(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)speaker_backend_is_enabled();
}

int32_t speakerIsRunning(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)speaker_backend_is_running();
}

int32_t speakerSetVolume(wasm_exec_env_t exec_env, int32_t v)
{
    (void)exec_env;
    if (v < 0 || v > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerSetVolume: volume out of range (0..255)");
        return kWasmErrInvalidArgument;
    }
    speaker_backend_set_volume((uint8_t)v);
    return kWasmOk;
}

int32_t speakerGetVolume(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)speaker_backend_get_volume();
}

int32_t speakerStop(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    speaker_backend_stop();
    return kWasmOk;
}

int32_t speakerTone(wasm_exec_env_t exec_env, float freq_hz, int32_t duration_ms)
{
    (void)exec_env;
    if (freq_hz <= 0.0f || freq_hz > 20000.0f) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerTone: frequency out of range (0..20000 Hz)");
        return kWasmErrInvalidArgument;
    }
    if (duration_ms < 0 || duration_ms > 60000) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerTone: duration out of range (0..60000 ms)");
        return kWasmErrInvalidArgument;
    }
    uint32_t duration = (duration_ms == 0) ? UINT32_MAX : (uint32_t)duration_ms;
    bool ok = speaker_backend_tone(freq_hz, duration);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "speakerTone: tone output failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t speakerBeeperStart(wasm_exec_env_t exec_env, float freq_hz, int32_t beep_count,
                              int32_t duration_ms, int32_t gap_ms, int32_t pause_ms)
{
    (void)exec_env;

    // Validate arguments
    if (freq_hz <= 0.0f || freq_hz > 20000.0f) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerBeeperStart: frequency out of range (0..20000 Hz)");
        return kWasmErrInvalidArgument;
    }
    if (beep_count < 1 || beep_count > 100) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerBeeperStart: beep_count out of range (1..100)");
        return kWasmErrInvalidArgument;
    }
    if (duration_ms < 10 || duration_ms > 10000) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerBeeperStart: duration_ms out of range (10..10000)");
        return kWasmErrInvalidArgument;
    }
    if (gap_ms < 0 || gap_ms > 10000) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerBeeperStart: gap_ms out of range (0..10000)");
        return kWasmErrInvalidArgument;
    }
    if (pause_ms < 0 || pause_ms > 60000) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "speakerBeeperStart: pause_ms out of range (0..60000)");
        return kWasmErrInvalidArgument;
    }

    // Check if already running
    if (g_beeper_running) {
        wasm_api_set_last_error(kWasmErrInternal, "speakerBeeperStart: beeper already running");
        return kWasmErrInternal;
    }

    // Store parameters
    g_beeper_freq = freq_hz;
    g_beeper_count = beep_count;
    g_beeper_duration = duration_ms;
    g_beeper_gap = gap_ms;
    g_beeper_pause = pause_ms;
    g_beeper_should_stop = false;

    // Create the beeper task
    BaseType_t ret = xTaskCreate(beeper_task, "beeper", 4096, NULL, 5, &g_beeper_task_handle);
    if (ret != pdPASS) {
        wasm_api_set_last_error(kWasmErrInternal, "speakerBeeperStart: failed to create beeper task");
        return kWasmErrInternal;
    }

    g_beeper_running = true;
    return kWasmOk;
}

int32_t speakerBeeperStop(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    if (!g_beeper_running) {
        return kWasmOk; // Not running, nothing to stop
    }

    // Signal the task to stop
    g_beeper_should_stop = true;

    // Wait for the task to finish
    int timeout = 100; // 1 second timeout (100 * 10ms)
    while (g_beeper_running && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }

    // If timeout, delete the task forcibly
    if (g_beeper_running && g_beeper_task_handle != nullptr) {
        vTaskDelete(g_beeper_task_handle);
        g_beeper_task_handle = nullptr;
        g_beeper_running = false;
    }

    // Ensure speaker is stopped
    speaker_backend_stop();

    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_speaker_native_symbols[] = {
    REG_NATIVE_FUNC(speakerBegin, "()i"),
    REG_NATIVE_FUNC(speakerEnd, "()i"),
    REG_NATIVE_FUNC(speakerIsEnabled, "()i"),
    REG_NATIVE_FUNC(speakerIsRunning, "()i"),
    REG_NATIVE_FUNC(speakerSetVolume, "(i)i"),
    REG_NATIVE_FUNC(speakerGetVolume, "()i"),
    REG_NATIVE_FUNC(speakerStop, "()i"),
    REG_NATIVE_FUNC(speakerTone, "(fi)i"),
    REG_NATIVE_FUNC(speakerBeeperStart, "(fiiii)i"),
    REG_NATIVE_FUNC(speakerBeeperStop, "()i"),
};
/* clang-format on */

} // namespace

extern "C" bool paperportal_speaker_is_enabled(void)
{
    return speaker_backend_is_enabled();
}

extern "C" bool paperportal_speaker_begin(void)
{
    return speaker_backend_begin();
}

extern "C" void paperportal_speaker_end(void)
{
    speaker_backend_end();
}

extern "C" bool paperportal_speaker_is_running(void)
{
    return speaker_backend_is_running();
}

extern "C" void paperportal_speaker_set_volume(uint8_t volume)
{
    speaker_backend_set_volume(volume);
}

extern "C" uint8_t paperportal_speaker_get_volume(void)
{
    return speaker_backend_get_volume();
}

extern "C" void paperportal_speaker_stop(void)
{
    speaker_backend_stop();
}

extern "C" bool paperportal_speaker_tone(float freq_hz, uint32_t duration_ms)
{
    return speaker_backend_tone(freq_hz, duration_ms);
}

bool wasm_api_register_speaker(void)
{
    const uint32_t count = sizeof(g_speaker_native_symbols) / sizeof(g_speaker_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_speaker", g_speaker_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_speaker natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_speaker: wasm_runtime_register_natives failed");
    }
    return ok;
}
