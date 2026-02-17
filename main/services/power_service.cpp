#include "services/power_service.h"

#include <inttypes.h>
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
#include "wasm/api/errors.h"

namespace {

constexpr bool usePng = false;
constexpr const char *kTag = "power_service";

extern const uint8_t _binary_sleepimage_jpg_start[] asm("_binary_sleepimage_jpg_start");
extern const uint8_t _binary_sleepimage_jpg_end[] asm("_binary_sleepimage_jpg_end");
extern const uint8_t _binary_sleepimage_png_start[] asm("_binary_sleepimage_png_start");
extern const uint8_t _binary_sleepimage_png_end[] asm("_binary_sleepimage_png_end");

constexpr gpio_num_t kPaperS3PowerHoldPin = GPIO_NUM_44;
constexpr int32_t kEpdMode4Bpp = 2;

void show_sleepimage()
{
    auto *display = Display::current();
    if (!display || display->driver() == PaperDisplayDriver::none) {
        return;
    }

    const uint8_t *start = usePng ? _binary_sleepimage_png_start : _binary_sleepimage_jpg_start;
    const uint8_t *end = usePng ? _binary_sleepimage_png_end : _binary_sleepimage_jpg_end;
    if (!start || !end || end <= start) {
        ESP_LOGW(kTag, "sleep image asset missing/empty");
        return;
    }
    const size_t len = (size_t)(end - start);

    int32_t desired_rotation = 1; // portrait on FastEPD
    if (display->driver() == PaperDisplayDriver::lgfx) {
        // In LGFX mode, pick a rotation that results in a portrait geometry (w < h) so that
        // the portrait-authored sleep image fills the screen correctly.
        const int32_t candidates[] = {0, 1, 2, 3};
        for (const int32_t candidate : candidates) {
            const int32_t cand_rc = display->setRotation(nullptr, candidate);
            if (cand_rc != kWasmOk) {
                continue;
            }
            const int32_t cw = display->width(nullptr);
            const int32_t ch = display->height(nullptr);
            if (cw > 0 && ch > 0 && cw < ch) {
                desired_rotation = candidate;
                break;
            }
        }
    }

    const int32_t rot_rc = display->setRotation(nullptr, desired_rotation);
    if (rot_rc != kWasmOk) {
        ESP_LOGW(kTag, "sleep image: setRotation(%" PRId32 ") failed rc=%" PRId32, desired_rotation, rot_rc);
    }

    const int32_t mode_rc = display->setEpdMode(nullptr, kEpdMode4Bpp);
    if (mode_rc != kWasmOk) {
        ESP_LOGW(kTag, "sleep image: setEpdMode(%ld) failed rc=%" PRId32, (long)kEpdMode4Bpp, mode_rc);
    }

    const int32_t fill_rc = display->fillScreen(nullptr, 0xFFFFFF);
    if (fill_rc != kWasmOk) {
        ESP_LOGW(kTag, "sleep image: fillScreen failed rc=%" PRId32, fill_rc);
    }

    const int32_t w = display->width(nullptr);
    const int32_t h = display->height(nullptr);
    if (w <= 0 || h <= 0) {
        ESP_LOGW(kTag, "sleep image: invalid display size w=%" PRId32 " h=%" PRId32, w, h);
        return;
    }

    int32_t draw_rc = kWasmErrInternal;
    if (usePng) {
        draw_rc = display->drawPngFit(nullptr, start, len, 0, 0, w, h);
    } else {
        draw_rc = display->drawJpgFit(nullptr, start, len, 0, 0, w, h);
    }
    if (draw_rc != kWasmOk) {
        ESP_LOGW(kTag, "sleep image: draw failed rc=%" PRId32, draw_rc);
    }

    const int32_t flush_rc = display->fullUpdateSlow(nullptr);
    if (flush_rc != kWasmOk) {
        ESP_LOGW(kTag, "sleep image: fullUpdateSlow failed rc=%" PRId32, flush_rc);
    }

    const int32_t wait_rc = display->waitDisplay(nullptr);
    if (wait_rc != kWasmOk) {
        ESP_LOGW(kTag, "sleep image: waitDisplay failed rc=%" PRId32, wait_rc);
    }
}

} // namespace

namespace power_service {

esp_err_t power_off(bool show_sleep_image)
{
    if (!paper_display_ensure_init()) {
        ESP_LOGW(kTag, "power off: display init failed");
        return ESP_FAIL;
    }

    if (show_sleep_image) {
        show_sleepimage();
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
