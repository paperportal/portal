#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Shared I2C helpers for runner peripherals (RTC/IMU/etc).
//
// Important:
// - When running FastEPD, the firmware still initializes LovyanGFX's GT911 touch
//   backend for input polling. LovyanGFX claims/configures the I2C peripheral.
// - To avoid "bus already in use" conflicts, all runner I2C access should go
//   through this module (which uses the same low-level I2C implementation as
//   LovyanGFX).
esp_err_t paper_i2c_init(void);

// Probe an address on the shared I2C bus.
// Returns ESP_OK if ACKed, ESP_ERR_NOT_FOUND if NACKed.
esp_err_t paper_i2c_probe(uint16_t address, int timeout_ms);

esp_err_t paper_i2c_write(uint16_t address, const uint8_t *data, size_t len, uint32_t freq_hz);

esp_err_t paper_i2c_read(uint16_t address, uint8_t *out, size_t out_len, uint32_t freq_hz);

esp_err_t paper_i2c_write_read(uint16_t address,
    const uint8_t *wdata,
    size_t wlen,
    uint8_t *rdata,
    size_t rlen,
    uint32_t freq_hz);
