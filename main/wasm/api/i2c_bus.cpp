#include "i2c_bus.h"

#include "esp_log.h"

#include "lgfx/v1/platforms/common.hpp"

namespace {

constexpr const char *kTag = "paper_i2c_bus";

// M5PaperS3 internal I2C (from M5Unified/M5GFX): SDA=GPIO41, SCL=GPIO42.
constexpr int kI2cPort = 1; // I2C_NUM_1
constexpr int kI2cSda = 41; // GPIO_NUM_41
constexpr int kI2cScl = 42; // GPIO_NUM_42
constexpr uint32_t kDefaultFreqHz = 400000;

bool g_pins_set = false;

} // namespace

static esp_err_t map_lgfx_i2c_error(lgfx::error_t err, bool is_probe)
{
    switch (err) {
    case lgfx::error_t::invalid_arg:
        return ESP_ERR_INVALID_ARG;
    case lgfx::error_t::connection_lost:
        return is_probe ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    case lgfx::error_t::mode_mismatch:
        return ESP_ERR_INVALID_STATE;
    case lgfx::error_t::periph_device_err:
        return ESP_ERR_INVALID_STATE;
    default:
        return ESP_FAIL;
    }
}

esp_err_t paper_i2c_init(void)
{
    if (g_pins_set) {
        return ESP_OK;
    }

    // Use LovyanGFX's I2C backend (the same one used by GT911 touch polling),
    // but only set the pins here. Avoid calling lgfx::i2c::init() because it
    // may tear down/reinitialize an already-active bus.
    auto res = lgfx::i2c::setPins(kI2cPort, kI2cSda, kI2cScl);
    if (!res.has_value()) {
        ESP_LOGW(kTag, "lgfx::i2c::setPins failed err=%d", (int)res.error());
        return map_lgfx_i2c_error(res.error(), false);
    }

    g_pins_set = true;
    return ESP_OK;
}

esp_err_t paper_i2c_probe(uint16_t address, int timeout_ms)
{
    (void)timeout_ms;
    if (address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = paper_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    auto res = lgfx::i2c::transactionWrite(kI2cPort, (int)address, nullptr, 0, kDefaultFreqHz);
    if (res.has_value()) {
        return ESP_OK;
    }
    return map_lgfx_i2c_error(res.error(), true);
}

esp_err_t paper_i2c_write(uint16_t address, const uint8_t *data, size_t len, uint32_t freq_hz)
{
    if (address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > UINT8_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = paper_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    auto res = lgfx::i2c::transactionWrite(kI2cPort, (int)address, data, (uint8_t)len, freq_hz);
    if (res.has_value()) {
        return ESP_OK;
    }
    return map_lgfx_i2c_error(res.error(), false);
}

esp_err_t paper_i2c_read(uint16_t address, uint8_t *out, size_t out_len, uint32_t freq_hz)
{
    if (address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!out && out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_len > UINT8_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = paper_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    auto res = lgfx::i2c::transactionRead(kI2cPort, (int)address, out, (uint8_t)out_len, freq_hz);
    if (res.has_value()) {
        return ESP_OK;
    }
    return map_lgfx_i2c_error(res.error(), false);
}

esp_err_t paper_i2c_write_read(uint16_t address,
    const uint8_t *wdata,
    size_t wlen,
    uint8_t *rdata,
    size_t rlen,
    uint32_t freq_hz)
{
    if (address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wdata && wlen) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!rdata && rlen) {
        return ESP_ERR_INVALID_ARG;
    }
    if (wlen > UINT8_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = paper_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    auto res = lgfx::i2c::transactionWriteRead(
        kI2cPort, (int)address, wdata, (uint8_t)wlen, rdata, rlen, freq_hz);
    if (res.has_value()) {
        return ESP_OK;
    }
    return map_lgfx_i2c_error(res.error(), false);
}
