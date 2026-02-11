#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "services/devserver_service.h"
#include "services/settings_service.h"
#include "host/event_loop.h"
#include "other/mem_utils.h"
#include "wasm/wasm_controller.h"
#include "sd_card.h"
#include <dirent.h>

#include <FastEPD.h>
#include <JPEGDEC.h>

static const char *kTag = "paperportal-runner";

extern "C" {
int JPEG_openRAM(JPEGIMAGE *pJPEG, uint8_t *pData, int iDataSize, JPEG_DRAW_CALLBACK *pfnDraw);
int JPEG_decode(JPEGIMAGE *pJPEG, int x, int y, int iOptions);
int JPEG_decodeDither(JPEGIMAGE *pJPEG, uint8_t *pDither, int iOptions);
void JPEG_close(JPEGIMAGE *pJPEG);
int JPEG_getLastError(JPEGIMAGE *pJPEG);
void JPEG_setPixelType(JPEGIMAGE *pJPEG, int iType);
}

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

esp_err_t start_dev_server_autostart() {
    bool enabled = false;
    esp_err_t err = settings_service::get_developer_mode(&enabled);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "[start_dev_server_autostart] NVS read failed (%s)", esp_err_to_name(err));
        return err;
    }
    if (!enabled) {
        return ESP_OK;
    }
    ESP_LOGI(kTag, "[start_dev_server_autostart] Enqueueing devserver startup.");
    err = devserver::start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "[start_dev_server_autostart] autostart enqueue failed (%s)", esp_err_to_name(err));
    }
    return err;
}

namespace {

extern const uint8_t _binary_sleepimage_jpg_start[] asm("_binary_sleepimage_jpg_start");
extern const uint8_t _binary_sleepimage_jpg_end[] asm("_binary_sleepimage_jpg_end");

static FASTEPD *g_boot_epd = nullptr;

int boot_sleepimage_jpeg_draw(JPEGDRAW *pDraw)
{
    if (!g_boot_epd || !pDraw || !pDraw->pPixels) {
        return 0;
    }

    const int epd_w = g_boot_epd->width();
    const int epd_h = g_boot_epd->height();
    if (epd_w <= 0 || epd_h <= 0) {
        return 0;
    }

    const int src_block_w = pDraw->iWidthUsed > 0 ? pDraw->iWidthUsed : pDraw->iWidth;
    const int src_block_h = pDraw->iHeight;

    const int src_x0 = pDraw->x < 0 ? -pDraw->x : 0;
    const int src_y0 = pDraw->y < 0 ? -pDraw->y : 0;
    const int draw_x0 = pDraw->x < 0 ? 0 : pDraw->x;
    const int draw_y0 = pDraw->y < 0 ? 0 : pDraw->y;
    if (draw_x0 >= epd_w || draw_y0 >= epd_h) {
        return 1;
    }

    const int max_w = epd_w - draw_x0;
    const int max_h = epd_h - draw_y0;
    const int src_w = src_block_w - src_x0;
    const int src_h = src_block_h - src_y0;
    const int copy_w = src_w < max_w ? src_w : max_w;
    const int copy_h = src_h < max_h ? src_h : max_h;
    if (copy_w <= 0 || copy_h <= 0) {
        return 1;
    }

    if (pDraw->iBpp == 4) {
        const uint8_t *src = (const uint8_t *)pDraw->pPixels;
        const int src_pitch = (pDraw->iWidth + 1) / 2;
        for (int yy = 0; yy < copy_h; ++yy) {
            const uint8_t *src_row = &src[(src_y0 + yy) * src_pitch];
            for (int xx = 0; xx < copy_w; ++xx) {
                const int sx = src_x0 + xx;
                const uint8_t packed = src_row[sx / 2];
                const uint8_t v4 = (uint8_t)((sx & 1) ? (packed & 0x0F) : (packed >> 4));
                g_boot_epd->drawPixelFast(draw_x0 + xx, draw_y0 + yy, v4);
            }
        }
    } else {
        // Fallback: treat as 8-bit grayscale (0..255) and truncate to 4bpp (0..15).
        const uint8_t *src = (const uint8_t *)pDraw->pPixels;
        for (int yy = 0; yy < copy_h; ++yy) {
            const uint8_t *src_row = &src[(src_y0 + yy) * pDraw->iWidth + src_x0];
            for (int xx = 0; xx < copy_w; ++xx) {
                const uint8_t v4 = (uint8_t)(src_row[xx] >> 4);
                g_boot_epd->drawPixelFast(draw_x0 + xx, draw_y0 + yy, v4);
            }
        }
    }

    return 1;
}

} // namespace

void show_sleepimage_with_fastepd_best_effort(void)
{
    constexpr int kPortraitRotationDeg = 90;

    const uint8_t *start = _binary_sleepimage_jpg_start;
    const uint8_t *end = _binary_sleepimage_jpg_end;
    if (end <= start) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] sleepimage asset missing/empty");
        return;
    }

    FASTEPD epaper;
    const int rc = epaper.initPanel(BB_PANEL_M5PAPERS3);
    if (rc != BBEP_SUCCESS) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] FastEPD initPanel failed (%d)", rc);
        return;
    }

    epaper.setMode(BB_MODE_4BPP);
    epaper.setRotation(kPortraitRotationDeg);
    epaper.fillScreen(0xF);

    uint8_t *fb = epaper.currentBuffer();
    if (!fb) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] FastEPD framebuffer missing");
        epaper.deInit();
        bbepDeinitBus();
        return;
    }

    g_boot_epd = &epaper;
    (void)fb;

    const size_t len = (size_t)(end - start);
    JPEGIMAGE jpeg{};
    bool ok = JPEG_openRAM(&jpeg, (uint8_t *)start, (int)len, boot_sleepimage_jpeg_draw) != 0;
    if (!ok) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] JPEG openRAM failed (%d)", (int)JPEG_getLastError(&jpeg));
        g_boot_epd = nullptr;
        epaper.deInit();
        bbepDeinitBus();
        return;
    }

    int mcu_w = 8;
    int mcu_h = 8;
    switch (jpeg.ucSubSample) {
        case 0x12:
            mcu_w = 8;
            mcu_h = 16;
            break;
        case 0x21:
            mcu_w = 16;
            mcu_h = 8;
            break;
        case 0x22:
            mcu_w = 16;
            mcu_h = 16;
            break;
        default:
            // Includes grayscale and 4:4:4.
            mcu_w = 8;
            mcu_h = 8;
            break;
    }
    const size_t aligned_w = (size_t)(((jpeg.iWidth + mcu_w - 1) / mcu_w) * mcu_w);
    const size_t dither_buf_len = aligned_w * (size_t)mcu_h;
    uint8_t *dither_buf = (uint8_t *)malloc(dither_buf_len);
    if (!dither_buf) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] dither buffer alloc failed (%u bytes)", (unsigned)dither_buf_len);
        JPEG_setPixelType(&jpeg, EIGHT_BIT_GRAYSCALE);
        ok = JPEG_decode(&jpeg, 0, 0, 0) != 0;
    } else {
        JPEG_setPixelType(&jpeg, FOUR_BIT_DITHERED);
        ok = JPEG_decodeDither(&jpeg, dither_buf, 0) != 0;
        free(dither_buf);
    }
    const int last_err = JPEG_getLastError(&jpeg);
    JPEG_close(&jpeg);
    g_boot_epd = nullptr;

    if (!ok) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] JPEG decode failed (%d)", last_err);
        epaper.deInit();
        bbepDeinitBus();
        return;
    }

    epaper.fullUpdate(CLEAR_SLOW);
    epaper.deInit();
    bbepDeinitBus();
}

extern "C" void app_main() {
    ESP_LOGI(kTag, "[app_main] Starting up.");
    mem_utils::init();

    ESP_LOGI(kTag, "[app_main] Application started. Initializing NVS.");
    init_nvs();
    ESP_LOGI(kTag, "[app_main] Mounting SD card.");
    if (sd_card_mount()) {
        ESP_LOGI(kTag, "[app_main] SD card mounted successfully.");
    } else {
        ESP_LOGW(kTag, "[app_main] SD card mount failed or no card present.");
    }

    //show_sleepimage_with_fastepd_best_effort();

    ESP_LOGI(kTag, "[app_main] Creating WASM controller.");
    static WasmController wasm;
    wasm_api_set_controller(&wasm);
    mem_utils::log_heap_brief(kTag, "[app_main] startup");
    ESP_LOGI(kTag, "[app_main] Starting event loop.");
    if (!host_event_loop_start(&wasm)) {
        ESP_LOGE(kTag, "[app_main] Failed to start host event loop");
        return;
    }
    ESP_LOGI(kTag, "[app_main] Event loop started. Starting devserver if enabled.");
    start_dev_server_autostart();

    ESP_LOGI(kTag, "[app_main] Looping forever...");
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }

}
