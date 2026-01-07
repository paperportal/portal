#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Shared I2C master bus for runner peripherals (RTC/IMU/etc).
esp_err_t paper_i2c_get_bus(i2c_master_bus_handle_t *out_bus);

// Probe an address on the shared I2C bus.
// Returns ESP_OK if ACKed, ESP_ERR_NOT_FOUND if NACKed.
esp_err_t paper_i2c_probe(uint16_t address, int timeout_ms);

SemaphoreHandle_t paper_i2c_get_mutex(void);

