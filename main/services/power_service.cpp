#include "services/power_service.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "m5papers3_display.h"
#include "wasm/api/display.h"
#include "wasm/api/display_fastepd.h"
#include "wasm/api/errors.h"

namespace {

constexpr bool usePng = false;
constexpr const char *kTag = "power_service";

extern const uint8_t _binary_sleepimage_jpg_start[] asm("_binary_sleepimage_jpg_start");
extern const uint8_t _binary_sleepimage_jpg_end[] asm("_binary_sleepimage_jpg_end");
extern const uint8_t _binary_sleepimage_png_start[] asm("_binary_sleepimage_png_start");
extern const uint8_t _binary_sleepimage_png_end[] asm("_binary_sleepimage_png_end");

constexpr gpio_num_t kPaperS3PowerHoldPin = GPIO_NUM_44;
constexpr uint32_t kSleepImageTaskStackBytes = 32 * 1024;
constexpr int32_t kLgfxEpdMode4Bpp = 2;
constexpr int32_t kFastEpdMode4Bpp = 2;

struct SleepImageDrawJob {
    const uint8_t *start = nullptr;
    size_t len = 0;
    int32_t draw_rc = kWasmErrInternal;
    int32_t display_rc = kWasmErrInternal;
    TaskHandle_t caller = nullptr;
};

extern "C" void show_sleepimage_with_fastepd_best_effort(void);

void sleepimage_draw_task(void *arg)
{
    auto *job = static_cast<SleepImageDrawJob *>(arg);
    if (!job) {
        vTaskDelete(nullptr);
        return;
    }

    show_sleepimage_with_fastepd_best_effort();
    xTaskNotifyGive(job->caller);
    vTaskDelete(nullptr);
}

} // namespace

namespace power_service {

esp_err_t power_off(bool show_sleepimage)
{
    if (!paper_display_ensure_init()) {
        ESP_LOGW(kTag, "power off: display init failed");
        return ESP_FAIL;
    }

    if (show_sleepimage) {
        show_sleepimage_with_fastepd_best_effort();
    }

    auto *display = Display::current();
    if (display && display->driver() == PaperDisplayDriver::lgfx) {
        paper_display().sleep();
        paper_display().waitDisplay();
    } else if (display && display->driver() != PaperDisplayDriver::none) {
        (void)display->waitDisplay(nullptr);
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Mirrors M5Unified's power-hold pulse sequence for M5PaperS3.
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << static_cast<uint32_t>(kPaperS3PowerHoldPin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    for (int i = 0; i < 5; ++i) {
        gpio_set_level(kPaperS3PowerHoldPin, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(kPaperS3PowerHoldPin, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_deep_sleep_start();
    esp_light_sleep_start();
    esp_restart();
    return ESP_OK; // not reached
}

} // namespace power_service
