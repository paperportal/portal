#include "sd_card.h"

#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace {

constexpr const char *kTag = "sd_card";
constexpr const char *kMountPoint = "/sdcard";

// Conservative defaults for M5Paper S3 (used if M5 pin mapping is unavailable).
constexpr int kDefaultSclk = 39;
constexpr int kDefaultMosi = 38;
constexpr int kDefaultMiso = 40;
constexpr int kDefaultCs = 47;

bool g_mounted = false;
sdmmc_card_t *g_card = nullptr;
spi_host_device_t g_host = SPI2_HOST;
bool g_bus_owned = false;

struct SdSpiPins {
    int sclk;
    int mosi;
    int miso;
    int cs;
};

SdSpiPins get_sd_spi_pins(void)
{
    // M5Paper S3 SD SPI pins (matches M5Unified's board_M5PaperS3 mapping).
    return SdSpiPins{ kDefaultSclk, kDefaultMosi, kDefaultMiso, kDefaultCs };
}

bool init_and_mount_on_host(spi_host_device_t host_id)
{
    const SdSpiPins pins = get_sd_spi_pins();

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = pins.mosi;
    bus_cfg.miso_io_num = pins.miso;
    bus_cfg.sclk_io_num = pins.sclk;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = (int)host_id;

    esp_err_t err = spi_bus_initialize(host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
    bool bus_owned = false;
    if (err == ESP_OK) {
        bus_owned = true;
    }
    else if (err == ESP_ERR_INVALID_STATE) {
        // Bus already initialized elsewhere (e.g. by M5Unified). Try to reuse it.
        bus_owned = false;
    }
    else {
        ESP_LOGW(kTag, "spi_bus_initialize(host=%d) failed: %s", (int)host_id, esp_err_to_name(err));
        return false;
    }

    // SD cards in SPI mode require pullups on CMD and DAT0/MISO.
    gpio_set_pull_mode((gpio_num_t)pins.mosi, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)pins.miso, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)pins.cs, GPIO_PULLUP_ONLY);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 8;
    mount_config.allocation_unit_size = 16 * 1024;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)pins.cs;
    slot_config.host_id = host_id;

    sdmmc_card_t *card = nullptr;
    err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK) {
        if (bus_owned) {
            spi_bus_free(host_id);
        }

        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(kTag, "SD already mounted");
        }
        else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(kTag, "No SD card detected");
        }
        else {
            ESP_LOGW(kTag, "esp_vfs_fat_sdspi_mount failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    g_card = card;
    g_host = host_id;
    g_bus_owned = bus_owned;
    g_mounted = true;

    ESP_LOGI(kTag, "Mounted SD at %s (host=%d, pins sclk=%d mosi=%d miso=%d cs=%d)",
             kMountPoint, (int)host_id, pins.sclk, pins.mosi, pins.miso, pins.cs);
    sdmmc_card_print_info(stdout, g_card);
    return true;
}

} // namespace

const char *sd_card_mount_point(void)
{
    return kMountPoint;
}

bool sd_card_mount(void)
{
    if (g_mounted) {
        return true;
    }

    if (init_and_mount_on_host(SPI2_HOST)) {
        return true;
    }
    if (init_and_mount_on_host(SPI3_HOST)) {
        return true;
    }

    return false;
}

void sd_card_unmount(void)
{
    if (!g_mounted) {
        return;
    }

    esp_vfs_fat_sdcard_unmount(kMountPoint, g_card);
    g_card = nullptr;
    g_mounted = false;

    if (g_bus_owned) {
        spi_bus_free(g_host);
    }
    g_bus_owned = false;
}

bool sd_card_is_mounted(void)
{
    return g_mounted;
}

const sdmmc_card_t *sd_card_get_card(void)
{
    return g_card;
}
