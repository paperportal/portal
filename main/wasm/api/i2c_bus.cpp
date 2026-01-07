#include "i2c_bus.h"

#include "driver/gpio.h"
#include "esp_log.h"

namespace {

constexpr const char *kTag = "paper_i2c_bus";

// M5PaperS3 internal I2C (from M5Unified/M5GFX): SDA=GPIO41, SCL=GPIO42.
constexpr i2c_port_num_t kI2cPort = I2C_NUM_1;
constexpr gpio_num_t kI2cSda = GPIO_NUM_41;
constexpr gpio_num_t kI2cScl = GPIO_NUM_42;

i2c_master_bus_handle_t g_bus = nullptr;
SemaphoreHandle_t g_mutex = nullptr;
StaticSemaphore_t g_mutex_storage;

} // namespace

SemaphoreHandle_t paper_i2c_get_mutex(void)
{
    if (g_mutex) {
        return g_mutex;
    }
    g_mutex = xSemaphoreCreateMutexStatic(&g_mutex_storage);
    return g_mutex;
}

esp_err_t paper_i2c_get_bus(i2c_master_bus_handle_t *out_bus)
{
    if (!out_bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_bus) {
        *out_bus = g_bus;
        return ESP_OK;
    }

    if (!paper_i2c_get_mutex()) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = kI2cPort;
    bus_cfg.sda_io_num = kI2cSda;
    bus_cfg.scl_io_num = kI2cScl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.intr_priority = 0;
    bus_cfg.trans_queue_depth = 0;
    bus_cfg.flags.enable_internal_pullup = 1;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &g_bus);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    *out_bus = g_bus;
    return ESP_OK;
}

esp_err_t paper_i2c_probe(uint16_t address, int timeout_ms)
{
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t err = paper_i2c_get_bus(&bus);
    if (err != ESP_OK) {
        return err;
    }

    SemaphoreHandle_t mtx = paper_i2c_get_mutex();
    if (!mtx) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS((uint32_t)timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    err = i2c_master_probe(bus, address, timeout_ms);
    xSemaphoreGive(mtx);
    return err;
}
