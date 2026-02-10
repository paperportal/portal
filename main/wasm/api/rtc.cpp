#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"
#include "i2c_bus.h"

namespace {

constexpr const char *kTag = "wasm_api_rtc";

// M5Unified's RTC abstraction defaults to a PCF8563 RTC on most boards.
// For the runner, we talk to the PCF8563 directly over ESP-IDF I2C.
//
// M5PaperS3 wiring (from M5Unified/M5GFX):
// - Internal I2C: SDA=GPIO41, SCL=GPIO42, 400kHz
// - PCF8563 I2C address: 0x51
constexpr uint32_t kRtcI2cFreqHz = 400000;
constexpr uint8_t kRtcI2cAddr = 0x51;
constexpr int kRtcI2cTimeoutMs = 100;

i2c_master_dev_handle_t g_rtc_dev = nullptr;

bool g_rtc_enabled = false;

struct RtcDateTime
{
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t week_day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t _pad;
};

static_assert(sizeof(RtcDateTime) == 10, "RtcDateTime layout must stay stable");

uint8_t bcd2_to_byte(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

uint8_t byte_to_bcd2(uint8_t value)
{
    uint8_t bcd_high = value / 10;
    return (uint8_t)((bcd_high << 4) | (value - (bcd_high * 10)));
}

esp_err_t ensure_i2c_initialized(void)
{
    if (g_rtc_dev) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t err = paper_i2c_get_bus(&bus);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kRtcI2cAddr;
    dev_cfg.scl_speed_hz = kRtcI2cFreqHz;

    return i2c_master_bus_add_device(bus, &dev_cfg, &g_rtc_dev);
}

esp_err_t i2c_write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!g_rtc_dev) {
        esp_err_t err = ensure_i2c_initialized();
        if (err != ESP_OK) {
            return err;
        }
    }

    SemaphoreHandle_t mtx = paper_i2c_get_mutex();
    if (!mtx) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS((uint32_t)kRtcI2cTimeoutMs)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t buf[1 + 32] = {0};
    if (len > sizeof(buf) - 1) {
        xSemaphoreGive(mtx);
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    if (data && len) {
        memcpy(&buf[1], data, len);
    }
    esp_err_t err = i2c_master_transmit(g_rtc_dev, buf, 1 + len, kRtcI2cTimeoutMs);
    xSemaphoreGive(mtx);
    return err;
}

esp_err_t i2c_read_reg(uint8_t reg, uint8_t *out, size_t out_len)
{
    if (!g_rtc_dev) {
        esp_err_t err = ensure_i2c_initialized();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!out && out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mtx = paper_i2c_get_mutex();
    if (!mtx) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS((uint32_t)kRtcI2cTimeoutMs)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = i2c_master_transmit_receive(g_rtc_dev, &reg, 1, out, out_len, kRtcI2cTimeoutMs);
    xSemaphoreGive(mtx);
    return err;
}

esp_err_t read_reg8(uint8_t reg, uint8_t *out)
{
    return i2c_read_reg(reg, out, 1);
}

esp_err_t write_reg8(uint8_t reg, uint8_t value)
{
    return i2c_write_reg(reg, &value, 1);
}

esp_err_t write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    return i2c_write_reg(reg, data, len);
}

esp_err_t read_reg(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_read_reg(reg, out, len);
}

int weekday_from_date(int32_t year, int32_t month, int32_t day)
{
    // Match M5Unified RTC_Class's weekday auto-adjust formula.
    if (month < 3) {
        year--;
        month += 12;
    }
    int32_t ydiv100 = year / 100;
    return (year + (year >> 2) - ydiv100 + (ydiv100 >> 2) + (13 * month + 8) / 5 + day) % 7;
}

int32_t rtcBegin(wasm_exec_env_t exec_env)
{
    (void)exec_env;

    if (g_rtc_enabled) {
        return kWasmOk;
    }

    esp_err_t err = ensure_i2c_initialized();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcBegin: i2c init failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcBegin: i2c init failed");
        return kWasmErrInternal;
    }

    // Probe first so boards without an RTC don't emit ESP-IDF "unexpected nack"
    // logs just because the demo tries to init it.
    err = paper_i2c_probe(kRtcI2cAddr, kRtcI2cTimeoutMs);
    if (err == ESP_ERR_NOT_FOUND) {
        wasm_api_set_last_error(kWasmErrNotFound, "rtcBegin: RTC not detected");
        return kWasmErrNotFound;
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcBegin: probe failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcBegin: probe failed");
        return kWasmErrInternal;
    }

    // Mirror M5Unified PCF8563_Class::begin():
    // - "Dummy" write (some boards occasionally failed without it)
    // - Initialize control registers
    err = write_reg8(0x00, 0x00);
    if (err == ESP_OK) {
        err = write_reg8(0x00, 0x00);
    }
    if (err == ESP_OK) {
        err = write_reg8(0x0E, 0x03);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcBegin: RTC init failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcBegin: RTC init failed");
        g_rtc_enabled = false;
        return kWasmErrInternal;
    }

    g_rtc_enabled = true;

    // Some boards sporadically NACK the first read immediately after the
    // control-register init writes. The old ESP_LOGI here "fixed" it by adding
    // latency, so make that delay explicit.
    vTaskDelay(pdMS_TO_TICKS(10));

    return kWasmOk;
}

int32_t rtcIsEnabled(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)g_rtc_enabled;
}

int32_t rtcGetDatetime(wasm_exec_env_t exec_env, uint8_t *out, size_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "rtcGetDatetime: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < sizeof(RtcDateTime)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "rtcGetDatetime: out_len too small");
        return kWasmErrInvalidArgument;
    }

    if (!g_rtc_enabled) {
        wasm_api_set_last_error(kWasmErrNotReady, "rtcGetDatetime: RTC not enabled");
        return kWasmErrNotReady;
    }

    uint8_t buf[7] = {0};
    esp_err_t err = read_reg(0x02, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcGetDatetime: read failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcGetDatetime: read failed");
        return kWasmErrInternal;
    }

    // PCF8563 register mapping matches M5Unified's PCF8563_Class.
    const uint8_t sec = bcd2_to_byte(buf[0] & 0x7F);
    const uint8_t min = bcd2_to_byte(buf[1] & 0x7F);
    const uint8_t hour = bcd2_to_byte(buf[2] & 0x3F);
    const uint8_t day = bcd2_to_byte(buf[3] & 0x3F);
    const uint8_t week_day = bcd2_to_byte(buf[4] & 0x07);
    const uint8_t month = bcd2_to_byte(buf[5] & 0x1F);
    const int16_t year = (int16_t)(bcd2_to_byte(buf[6]) + ((buf[5] & 0x80) ? 1900 : 2000));

    RtcDateTime dt = {
        .year = year,
        .month = month,
        .day = day,
        .week_day = week_day,
        .hour = hour,
        .minute = min,
        .second = sec,
        ._pad = 0,
    };
    memcpy(out, &dt, sizeof(dt));
    return (int32_t)sizeof(dt);
}

int32_t rtcSetDatetime(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    if (!ptr) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "rtcSetDatetime: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len < sizeof(RtcDateTime)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "rtcSetDatetime: len too small");
        return kWasmErrInvalidArgument;
    }

    if (!g_rtc_enabled) {
        wasm_api_set_last_error(kWasmErrNotReady, "rtcSetDatetime: RTC not enabled");
        return kWasmErrNotReady;
    }

    const RtcDateTime *dt = (const RtcDateTime *)ptr;

    uint8_t week_day = dt->week_day;
    if (week_day > 6 && dt->year >= 1900 && ((size_t)(dt->month - 1)) < 12) {
        week_day = (uint8_t)weekday_from_date(dt->year, dt->month, dt->day);
    }

    uint8_t buf[7] = {0};
    buf[0] = byte_to_bcd2(dt->second);
    buf[1] = byte_to_bcd2(dt->minute);
    buf[2] = byte_to_bcd2(dt->hour);
    buf[3] = byte_to_bcd2(dt->day);
    buf[4] = (uint8_t)(0x07u & week_day);
    buf[5] = (uint8_t)(byte_to_bcd2(dt->month) + ((dt->year < 2000) ? 0x80 : 0));
    buf[6] = byte_to_bcd2((uint8_t)(dt->year % 100));

    esp_err_t err = write_reg(0x02, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcSetDatetime: write failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcSetDatetime: write failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t rtcSetTimerIrq(wasm_exec_env_t exec_env, int32_t ms)
{
    (void)exec_env;
    if (!g_rtc_enabled) {
        wasm_api_set_last_error(kWasmErrNotReady, "rtcSetTimerIrq: RTC not enabled");
        return kWasmErrNotReady;
    }
    if (ms < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "rtcSetTimerIrq: ms < 0");
        return kWasmErrInvalidArgument;
    }

    uint8_t reg_value = 0;
    esp_err_t err = read_reg8(0x01, &reg_value);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcSetTimerIrq: read reg failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcSetTimerIrq: read failed");
        return kWasmErrInternal;
    }
    reg_value = (uint8_t)(reg_value & ~0x0C); // clear flag bits

    uint32_t after_seconds = ((uint32_t)ms + 500) / 1000;
    if (after_seconds == 0) {
        // Disable timer.
        err = write_reg8(0x01, (uint8_t)(reg_value & ~0x01));
        if (err == ESP_OK) {
            err = write_reg8(0x0E, 0x03);
        }
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "rtcSetTimerIrq: disable failed: %s", esp_err_to_name(err));
            wasm_api_set_last_error(kWasmErrInternal, "rtcSetTimerIrq: disable failed");
            return kWasmErrInternal;
        }
        return 0;
    }

    size_t div = 1;
    uint8_t type_value = 0x82;
    if (after_seconds < 270) {
        if (after_seconds > 255) {
            after_seconds = 255;
        }
    } else {
        div = 60;
        after_seconds = (after_seconds + 30) / (uint32_t)div;
        if (after_seconds > 255) {
            after_seconds = 255;
        }
        type_value = 0x83;
    }

    err = write_reg8(0x0E, type_value);
    if (err == ESP_OK) {
        err = write_reg8(0x0F, (uint8_t)after_seconds);
    }
    if (err == ESP_OK) {
        err = write_reg8(0x01, (uint8_t)((reg_value | 0x01) & ~0x80));
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcSetTimerIrq: set failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcSetTimerIrq: set failed");
        return kWasmErrInternal;
    }

    return (int32_t)(after_seconds * div * 1000);
}

int32_t rtcClearIrq(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (!g_rtc_enabled) {
        wasm_api_set_last_error(kWasmErrNotReady, "rtcClearIrq: RTC not enabled");
        return kWasmErrNotReady;
    }

    uint8_t reg_value = 0;
    esp_err_t err = read_reg8(0x01, &reg_value);
    if (err == ESP_OK) {
        reg_value = (uint8_t)(reg_value & ~0x0C);
        err = write_reg8(0x01, reg_value);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "rtcClearIrq: failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "rtcClearIrq: failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t rtcSetAlarmIrq(wasm_exec_env_t exec_env, int32_t seconds)
{
    (void)exec_env;
    if (!g_rtc_enabled) {
        wasm_api_set_last_error(kWasmErrNotReady, "rtcSetAlarmIrq: RTC not enabled");
        return kWasmErrNotReady;
    }
    if (seconds < 0 || seconds > 86400) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "rtcSetAlarmIrq: seconds out of range (0..86400)");
        return kWasmErrInvalidArgument;
    }

    // In M5Unified, setAlarmIRQ(int afterSeconds) is a deprecated wrapper that
    // delegates to setTimerIRQ(afterSeconds * 1000). Keep that behavior for
    // wasm apps that expect a relative wake timer.
    int32_t res = rtcSetTimerIrq(exec_env, seconds * 1000);
    if (res < 0) {
        return res;
    }
    return kWasmOk;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_rtc_native_symbols[] = {
    REG_NATIVE_FUNC(rtcBegin, "()i"),
    REG_NATIVE_FUNC(rtcIsEnabled, "()i"),
    REG_NATIVE_FUNC(rtcGetDatetime, "(*~)i"),
    REG_NATIVE_FUNC(rtcSetDatetime, "(*~)i"),
    REG_NATIVE_FUNC(rtcSetTimerIrq, "(i)i"),
    REG_NATIVE_FUNC(rtcClearIrq, "()i"),
    REG_NATIVE_FUNC(rtcSetAlarmIrq, "(i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_rtc(void)
{
    const uint32_t count = sizeof(g_rtc_native_symbols) / sizeof(g_rtc_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_rtc", g_rtc_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_rtc natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_rtc: wasm_runtime_register_natives failed");
    }
    return ok;
}
