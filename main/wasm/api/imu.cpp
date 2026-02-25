#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"
#include "i2c_bus.h"

namespace {

constexpr const char *kTag = "wasm_api_imu";

constexpr uint32_t kImuI2cFreqHz = 400000;
constexpr int kImuI2cTimeoutMs = 100;

struct I2cDeviceHandle
{
    uint8_t addr = 0;
    i2c_master_dev_handle_t dev = nullptr;
};

I2cDeviceHandle g_i2c_devs[4] = {};

esp_err_t ensure_i2c_initialized(void)
{
    i2c_master_bus_handle_t bus = nullptr;
    return paper_i2c_get_bus(&bus);
}

i2c_master_dev_handle_t ensure_i2c_dev(uint8_t dev_addr)
{
    for (auto &d : g_i2c_devs) {
        if (d.dev && d.addr == dev_addr) {
            return d.dev;
        }
    }

    i2c_master_bus_handle_t bus = nullptr;
    if (paper_i2c_get_bus(&bus) != ESP_OK) {
        return nullptr;
    }

    for (auto &d : g_i2c_devs) {
        if (d.dev == nullptr) {
            i2c_device_config_t dev_cfg = {};
            dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dev_cfg.device_address = dev_addr;
            dev_cfg.scl_speed_hz = kImuI2cFreqHz;

            if (i2c_master_bus_add_device(bus, &dev_cfg, &d.dev) != ESP_OK) {
                d.dev = nullptr;
                d.addr = 0;
                return nullptr;
            }
            d.addr = dev_addr;
            return d.dev;
        }
    }
    return nullptr;
}

esp_err_t i2c_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t dev = ensure_i2c_dev(dev_addr);
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t mtx = paper_i2c_get_mutex();
    if (!mtx) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS((uint32_t)kImuI2cTimeoutMs)) != pdTRUE) {
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
    esp_err_t err = i2c_master_transmit(dev, buf, 1 + len, kImuI2cTimeoutMs);
    xSemaphoreGive(mtx);
    return err;
}

esp_err_t i2c_read_reg(uint8_t dev_addr, uint8_t reg, uint8_t *out, size_t out_len)
{
    i2c_master_dev_handle_t dev = ensure_i2c_dev(dev_addr);
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!out && out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mtx = paper_i2c_get_mutex();
    if (!mtx) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS((uint32_t)kImuI2cTimeoutMs)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, out, out_len, kImuI2cTimeoutMs);
    xSemaphoreGive(mtx);
    return err;
}

esp_err_t write_reg8(uint8_t dev_addr, uint8_t reg, uint8_t value)
{
    return i2c_write_reg(dev_addr, reg, &value, 1);
}

esp_err_t read_reg8(uint8_t dev_addr, uint8_t reg, uint8_t *out)
{
    return i2c_read_reg(dev_addr, reg, out, 1);
}

static int16_t read_le_i16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

struct Vec3
{
    float x;
    float y;
    float z;
};
static_assert(sizeof(Vec3) == 12, "Vec3 layout must stay stable");

struct Temp
{
    float celsius;
};
static_assert(sizeof(Temp) == 4, "Temp layout must stay stable");

enum class ImuType : uint8_t {
    None = 0,
    Mpu6886Family,
    Sh200q,
    Bmi270,
};

struct ImuState
{
    ImuType type = ImuType::None;
    uint8_t addr = 0;
    uint32_t last_update_us = 0;
    int16_t raw_accel[3] = {0, 0, 0};
    int16_t raw_gyro[3] = {0, 0, 0};
    int16_t raw_temp = 0;

    // Conversion params (mirroring M5Unified defaults).
    float accel_res = 0.0f;      // g per LSB
    float gyro_res = 0.0f;       // dps per LSB
    float temp_res = 0.0f;       // degC per ADC LSB
    float temp_offset = 0.0f;    // degC offset
};

ImuState g_imu;

// M5Unified's IMU sensor_mask_t bit layout:
// accel=1<<0, gyro=1<<1, mag=1<<2.
constexpr int32_t kSensorMaskAccel = 1 << 0;
constexpr int32_t kSensorMaskGyro = 1 << 1;

// ===== MPU6886 / MPU6050 / MPU9250 (InvenSense) =====
namespace mpu {
constexpr uint8_t kAddr = 0x68;
constexpr uint8_t kRegWhoAmI = 0x75;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegIntStatus = 0x3A;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegTempOutH = 0x41;
constexpr uint8_t kIdMpu6886 = 0x19;
constexpr uint8_t kIdMpu6050 = 0x68;
constexpr uint8_t kIdMpu9250 = 0x71;
} // namespace mpu

bool mpu_begin(uint8_t addr)
{
    uint8_t id = 0;
    if (read_reg8(addr, mpu::kRegWhoAmI, &id) != ESP_OK) {
        return false;
    }
    if (id != mpu::kIdMpu6886 && id != mpu::kIdMpu6050 && id != mpu::kIdMpu9250) {
        return false;
    }

    // Port of M5Unified MPU6886_Class::begin():
    (void)write_reg8(addr, mpu::kRegPwrMgmt1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(10));

    static constexpr uint8_t init_cmd[] = {
        0x6B, 0x01, // PWR_MGMT_1
        0x1C, 0x10, // ACCEL_CONFIG: +-8G
        0x1B, 0x18, // GYRO_CONFIG: +-2000 dps
        0x1A, 0x01, // CONFIG
        0x19, 0x03, // SMPLRT_DIV
        0x37, 0xC0, // INT_PIN_CFG: active low, open-drain
        0x38, 0x00, // INT_ENABLE
        0x1D, 0x00, // ACCEL_CONFIG2
        0x6A, 0x00, // USER_CTRL
        0x23, 0x00, // FIFO_EN
        0xFF, 0xFF,
    };

    for (size_t idx = 0; idx + 1 < sizeof(init_cmd); idx += 2) {
        const uint8_t reg = init_cmd[idx];
        const uint8_t val = init_cmd[idx + 1];
        if ((reg & val) == 0xFF) {
            break;
        }
        uint8_t verify = 0;
        int retry = 16;
        do {
            (void)write_reg8(addr, reg, val);
            (void)read_reg8(addr, reg, &verify);
        } while (verify != val && --retry);
    }

    g_imu.type = ImuType::Mpu6886Family;
    g_imu.addr = addr;
    g_imu.accel_res = 8.0f / 32768.0f;
    g_imu.gyro_res = 2000.0f / 32768.0f;
    g_imu.temp_res = 1.0f / 326.8f;
    g_imu.temp_offset = 25.0f;
    g_imu.last_update_us = 0;
    return true;
}

bool mpu_update(void)
{
    uint8_t st = 0;
    if (read_reg8(g_imu.addr, mpu::kRegIntStatus, &st) != ESP_OK) {
        return false;
    }
    if ((st & 0x01) == 0) {
        return false;
    }

    uint8_t buf[14] = {0};
    if (i2c_read_reg(g_imu.addr, mpu::kRegAccelXoutH, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }

    g_imu.raw_accel[0] = (int16_t)((buf[0] << 8) | buf[1]);
    g_imu.raw_accel[1] = (int16_t)((buf[2] << 8) | buf[3]);
    g_imu.raw_accel[2] = (int16_t)((buf[4] << 8) | buf[5]);
    g_imu.raw_temp = (int16_t)((buf[6] << 8) | buf[7]);
    g_imu.raw_gyro[0] = (int16_t)((buf[8] << 8) | buf[9]);
    g_imu.raw_gyro[1] = (int16_t)((buf[10] << 8) | buf[11]);
    g_imu.raw_gyro[2] = (int16_t)((buf[12] << 8) | buf[13]);
    g_imu.last_update_us = (uint32_t)esp_timer_get_time();
    return true;
}

// ===== SH200Q =====
namespace sh200q {
constexpr uint8_t kAddr = 0x6C;
constexpr uint8_t kRegWhoAmI = 0x30;
} // namespace sh200q

bool sh200q_begin(uint8_t addr)
{
    uint8_t id = 0;
    if (read_reg8(addr, sh200q::kRegWhoAmI, &id) != ESP_OK || id != 0x18) {
        return false;
    }

    auto bit_on = [&](uint8_t reg, uint8_t mask) {
        uint8_t v = 0;
        if (read_reg8(addr, reg, &v) != ESP_OK) {
            return false;
        }
        v |= mask;
        return write_reg8(addr, reg, v) == ESP_OK;
    };
    auto bit_off = [&](uint8_t reg, uint8_t mask) {
        uint8_t v = 0;
        if (read_reg8(addr, reg, &v) != ESP_OK) {
            return false;
        }
        v &= (uint8_t)~mask;
        return write_reg8(addr, reg, v) == ESP_OK;
    };

    // Port of M5Unified SH200Q_Class::begin().
    (void)bit_on(0xC2, 0x04);
    vTaskDelay(1);
    (void)bit_off(0xC2, 0x04);
    vTaskDelay(1);
    (void)bit_on(0xD8, 0x80);
    vTaskDelay(1);
    (void)bit_off(0xD8, 0x80);
    vTaskDelay(1);

    static constexpr uint8_t init_cmd[] = {
        0x78, 0x61,
        0x78, 0x00,
        0x0E, 0x91, // ACC_CONFIG: 256Hz
        0x0F, 0x13, // GYRO_CONFIG: 500Hz
        0x11, 0x03, // GYRO_DLPF: 50Hz
        0x12, 0x00, // FIFO_CONFIG
        0x14, 0x20, // data ready interrupt en
        0x16, 0x01, // ACC_RANGE: +-8G
        0x2B, 0x00, // GYRO_RANGE: +-2000
        0xBA, 0xC0, // REG_SET1
        0xFF, 0xFF,
    };
    for (size_t idx = 0; idx + 1 < sizeof(init_cmd); idx += 2) {
        const uint8_t reg = init_cmd[idx];
        const uint8_t val = init_cmd[idx + 1];
        if ((reg & val) == 0xFF) {
            break;
        }
        (void)write_reg8(addr, reg, val);
        vTaskDelay(1);
    }
    (void)bit_on(0xCA, 0x10);
    vTaskDelay(1);
    (void)bit_off(0xCA, 0x10);
    vTaskDelay(1);

    g_imu.type = ImuType::Sh200q;
    g_imu.addr = addr;
    g_imu.accel_res = 8.0f / 32768.0f;
    g_imu.gyro_res = 2000.0f / 32768.0f;
    g_imu.temp_res = 1.0f / 333.87f;
    g_imu.temp_offset = 21.0f;
    g_imu.last_update_us = 0;
    return true;
}

bool sh200q_update(void)
{
    uint8_t st = 0;
    if (read_reg8(g_imu.addr, 0x2C, &st) != ESP_OK) {
        return false;
    }
    if ((st & 0x20u) == 0) {
        return false;
    }

    uint8_t buf[14] = {0};
    if (i2c_read_reg(g_imu.addr, 0x00, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }

    g_imu.raw_accel[0] = read_le_i16(&buf[0]);
    g_imu.raw_accel[1] = read_le_i16(&buf[2]);
    g_imu.raw_accel[2] = read_le_i16(&buf[4]);
    g_imu.raw_gyro[0] = read_le_i16(&buf[6]);
    g_imu.raw_gyro[1] = read_le_i16(&buf[8]);
    g_imu.raw_gyro[2] = read_le_i16(&buf[10]);
    g_imu.raw_temp = read_le_i16(&buf[12]);
    g_imu.last_update_us = (uint32_t)esp_timer_get_time();
    return true;
}

// ===== BMI270 =====
namespace bmi270 {
constexpr uint8_t kChipIdAddr = 0x00;
constexpr uint8_t kPwrConfAddr = 0x7C;
constexpr uint8_t kPwrCtrlAddr = 0x7D;
constexpr uint8_t kCmdRegAddr = 0x7E;
constexpr uint8_t kInitCtrlAddr = 0x59;
constexpr uint8_t kInitAddr0 = 0x5B;
constexpr uint8_t kInitDataAddr = 0x5E;
constexpr uint8_t kInternalStatusAddr = 0x21;
constexpr uint8_t kAccXlsbAddr = 0x0C;
constexpr uint8_t kTemp0Addr = 0x22;
constexpr uint8_t kSoftResetCmd = 0xB6;
constexpr uint8_t kDefaultAddr = 0x69;
} // namespace bmi270

#include "imu_bmi270_config.inl"

bool bmi270_upload_config(uint8_t addr)
{
    // Port of BMI270_Class::_upload_file(), but chunked for ESP-IDF I2C.
    constexpr size_t kChunk = 32;
    for (size_t index = 0; index < sizeof(bmi270_config_file); index += kChunk) {
        const size_t write_len = (sizeof(bmi270_config_file) - index < kChunk) ? (sizeof(bmi270_config_file) - index) : kChunk;

        const uint8_t addr_array[2] = {
            (uint8_t)((index >> 1) & 0x0F),
            (uint8_t)(index >> 5),
        };

        if (i2c_write_reg(addr, bmi270::kInitAddr0, addr_array, sizeof(addr_array)) != ESP_OK) {
            return false;
        }
        if (i2c_write_reg(addr, bmi270::kInitDataAddr, &bmi270_config_file[index], write_len) != ESP_OK) {
            return false;
        }
    }
    return true;
}

bool bmi270_begin(uint8_t addr)
{
    uint8_t id = 0;
    if (read_reg8(addr, bmi270::kChipIdAddr, &id) != ESP_OK || id != 0x24) {
        return false;
    }

    (void)write_reg8(addr, bmi270::kCmdRegAddr, bmi270::kSoftResetCmd);
    int retry = 16;
    do {
        vTaskDelay(1);
        (void)read_reg8(addr, bmi270::kPwrConfAddr, &id);
    } while (id == 0 && --retry);

    (void)write_reg8(addr, bmi270::kPwrConfAddr, 0x00); // power save disabled
    vTaskDelay(1);

    if (!bmi270_upload_config(addr)) {
        return false;
    }

    (void)write_reg8(addr, bmi270::kInitCtrlAddr, 0x01);
    // Ensure accel/gyro/temp are enabled (AUX disabled).
    (void)write_reg8(addr, bmi270::kPwrCtrlAddr, 0x0E);

    retry = 16;
    uint8_t st = 0;
    do {
        vTaskDelay(1);
        (void)read_reg8(addr, bmi270::kInternalStatusAddr, &st);
    } while (st == 0 && --retry);
    if (retry == 0) {
        return false;
    }

    g_imu.type = ImuType::Bmi270;
    g_imu.addr = addr;
    g_imu.accel_res = 8.0f / 32768.0f;
    g_imu.gyro_res = 2000.0f / 32768.0f;
    g_imu.temp_res = 1.0f / 512.0f;
    g_imu.temp_offset = 23.0f;
    g_imu.last_update_us = 0;
    return true;
}

bool bmi270_update(void)
{
    uint8_t buf[12] = {0};
    if (i2c_read_reg(g_imu.addr, bmi270::kAccXlsbAddr, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }

    g_imu.raw_accel[0] = read_le_i16(&buf[0]);
    g_imu.raw_accel[1] = read_le_i16(&buf[2]);
    g_imu.raw_accel[2] = read_le_i16(&buf[4]);
    g_imu.raw_gyro[0] = read_le_i16(&buf[6]);
    g_imu.raw_gyro[1] = read_le_i16(&buf[8]);
    g_imu.raw_gyro[2] = read_le_i16(&buf[10]);

    uint8_t tbuf[2] = {0};
    if (i2c_read_reg(g_imu.addr, bmi270::kTemp0Addr, tbuf, sizeof(tbuf)) == ESP_OK) {
        g_imu.raw_temp = read_le_i16(tbuf);
    }

    g_imu.last_update_us = (uint32_t)esp_timer_get_time();
    return true;
}

bool imu_update_internal(void)
{
    switch (g_imu.type) {
    case ImuType::Mpu6886Family:
        return mpu_update();
    case ImuType::Sh200q:
        return sh200q_update();
    case ImuType::Bmi270:
        return bmi270_update();
    case ImuType::None:
    default:
        return false;
    }
}

int32_t imuBegin(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (g_imu.type != ImuType::None) {
        return kWasmOk;
    }

    esp_err_t err = ensure_i2c_initialized();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "imuBegin: i2c init failed: %s", esp_err_to_name(err));
        wasm_api_set_last_error(kWasmErrInternal, "imuBegin: i2c init failed");
        return kWasmErrInternal;
    }

    // Mirror M5Unified's probing order:
    // 1) MPU6886/MPU6050/MPU9250 family @ 0x68
    // 2) BMI270 @ 0x68/0x69
    // 3) SH200Q @ 0x6C
    //
    // Use i2c_master_probe() first so missing devices don't spam ESP-IDF
    // "unexpected nack" error logs during normal startup.
    const bool has_0x68 = (paper_i2c_probe(0x68, kImuI2cTimeoutMs) == ESP_OK);
    if (has_0x68 && mpu_begin(mpu::kAddr)) {
        return kWasmOk;
    }

    if (paper_i2c_probe(bmi270::kDefaultAddr, kImuI2cTimeoutMs) == ESP_OK && bmi270_begin(bmi270::kDefaultAddr)) {
        return kWasmOk;
    }

    if (has_0x68 && bmi270_begin(0x68)) {
        return kWasmOk;
    }

    if (paper_i2c_probe(sh200q::kAddr, kImuI2cTimeoutMs) == ESP_OK && sh200q_begin(sh200q::kAddr)) {
        return kWasmOk;
    }

    wasm_api_set_last_error(kWasmErrNotFound, "imuBegin: no supported IMU detected");
    return kWasmErrNotFound;
}

int32_t imuIsEnabled(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)(g_imu.type != ImuType::None);
}

int32_t imuUpdate(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (g_imu.type == ImuType::None) {
        wasm_api_set_last_error(kWasmErrNotReady, "imuUpdate: IMU not enabled");
        return kWasmErrNotReady;
    }
    bool ok = imu_update_internal();
    return ok ? (kSensorMaskAccel | kSensorMaskGyro) : 0;
}

int32_t imuGetAccel(wasm_exec_env_t exec_env, uint8_t *out, size_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "imuGetAccel: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < sizeof(Vec3)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "imuGetAccel: out_len too small");
        return kWasmErrInvalidArgument;
    }
    if (g_imu.type == ImuType::None) {
        wasm_api_set_last_error(kWasmErrNotReady, "imuGetAccel: IMU not enabled");
        return kWasmErrNotReady;
    }

    const uint32_t now = (uint32_t)esp_timer_get_time();
    if (now - g_imu.last_update_us > 256) {
        (void)imu_update_internal();
    }

    Vec3 v = {
        .x = g_imu.raw_accel[0] * g_imu.accel_res,
        .y = g_imu.raw_accel[1] * g_imu.accel_res,
        .z = g_imu.raw_accel[2] * g_imu.accel_res,
    };
    memcpy(out, &v, sizeof(v));
    return (int32_t)sizeof(v);
}

int32_t imuGetGyro(wasm_exec_env_t exec_env, uint8_t *out, size_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "imuGetGyro: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < sizeof(Vec3)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "imuGetGyro: out_len too small");
        return kWasmErrInvalidArgument;
    }
    if (g_imu.type == ImuType::None) {
        wasm_api_set_last_error(kWasmErrNotReady, "imuGetGyro: IMU not enabled");
        return kWasmErrNotReady;
    }

    const uint32_t now = (uint32_t)esp_timer_get_time();
    if (now - g_imu.last_update_us > 256) {
        (void)imu_update_internal();
    }

    Vec3 v = {
        .x = g_imu.raw_gyro[0] * g_imu.gyro_res,
        .y = g_imu.raw_gyro[1] * g_imu.gyro_res,
        .z = g_imu.raw_gyro[2] * g_imu.gyro_res,
    };
    memcpy(out, &v, sizeof(v));
    return (int32_t)sizeof(v);
}

int32_t imuGetTemp(wasm_exec_env_t exec_env, uint8_t *out, size_t out_len)
{
    (void)exec_env;
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "imuGetTemp: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < sizeof(Temp)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "imuGetTemp: out_len too small");
        return kWasmErrInvalidArgument;
    }
    if (g_imu.type == ImuType::None) {
        wasm_api_set_last_error(kWasmErrNotReady, "imuGetTemp: IMU not enabled");
        return kWasmErrNotReady;
    }

    const uint32_t now = (uint32_t)esp_timer_get_time();
    if (now - g_imu.last_update_us > 256) {
        (void)imu_update_internal();
    }

    Temp temp = { .celsius = g_imu.raw_temp * g_imu.temp_res + g_imu.temp_offset };
    memcpy(out, &temp, sizeof(temp));
    return (int32_t)sizeof(temp);
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_imu_native_symbols[] = {
    REG_NATIVE_FUNC(imuBegin, "()i"),
    REG_NATIVE_FUNC(imuIsEnabled, "()i"),
    REG_NATIVE_FUNC(imuUpdate, "()i"),
    REG_NATIVE_FUNC(imuGetAccel, "(*~)i"),
    REG_NATIVE_FUNC(imuGetGyro, "(*~)i"),
    REG_NATIVE_FUNC(imuGetTemp, "(*~)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_imu(void)
{
    const uint32_t count = sizeof(g_imu_native_symbols) / sizeof(g_imu_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("portal_imu", g_imu_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register portal_imu natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_imu: wasm_runtime_register_natives failed");
    }
    return ok;
}
