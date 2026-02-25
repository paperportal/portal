#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <FastEPD.h>
#include <JPEGDEC.h>
#include "lgfx/utility/lgfx_pngle.h"

#include "display_fastepd.h"
#include "display_fastepd_arc.h"

#include "esp_log.h"

#include "../api.h"
#include "errors.h"

extern const uint8_t _binary_inter_medium_8_bbf[] asm("_binary_inter_medium_8_bbf_start");
extern const uint8_t _binary_inter_medium_9_bbf[] asm("_binary_inter_medium_8_bbf_start");
extern const uint8_t _binary_inter_medium_10_bbf[] asm("_binary_inter_medium_10_bbf_start");
extern const uint8_t _binary_inter_medium_11_bbf[] asm("_binary_inter_medium_10_bbf_start");
extern const uint8_t _binary_inter_medium_12_bbf[] asm("_binary_inter_medium_12_bbf_start");
extern const uint8_t _binary_inter_medium_13_bbf[] asm("_binary_inter_medium_12_bbf_start");
extern const uint8_t _binary_inter_medium_14_bbf[] asm("_binary_inter_medium_14_bbf_start");
extern const uint8_t _binary_inter_medium_15_bbf[] asm("_binary_inter_medium_14_bbf_start");
extern const uint8_t _binary_inter_medium_16_bbf[] asm("_binary_inter_medium_16_bbf_start");
extern const uint8_t _binary_inter_medium_18_bbf[] asm("_binary_inter_medium_18_bbf_start");
extern const uint8_t _binary_inter_medium_20_bbf[] asm("_binary_inter_medium_20_bbf_start");
extern const uint8_t _binary_inter_medium_22_bbf[] asm("_binary_inter_medium_22_bbf_start");
extern const uint8_t _binary_inter_medium_24_bbf[] asm("_binary_inter_medium_24_bbf_start");
extern const uint8_t _binary_inter_medium_26_bbf[] asm("_binary_inter_medium_26_bbf_start");
extern const uint8_t _binary_inter_medium_28_bbf[] asm("_binary_inter_medium_28_bbf_start");
extern const uint8_t _binary_inter_medium_30_bbf[] asm("_binary_inter_medium_30_bbf_start");
extern const uint8_t _binary_inter_medium_32_bbf[] asm("_binary_inter_medium_32_bbf_start");

extern void hold_pwroff_pulse_low();

extern "C" {
int JPEG_openRAM(JPEGIMAGE *pJPEG, uint8_t *pData, int iDataSize, JPEG_DRAW_CALLBACK *pfnDraw);
int JPEG_getWidth(JPEGIMAGE *pJPEG);
int JPEG_getHeight(JPEGIMAGE *pJPEG);
int JPEG_getSubSample(JPEGIMAGE *pJPEG);
void JPEG_setPixelType(JPEGIMAGE *pJPEG, int iType);
int JPEG_decode(JPEGIMAGE *pJPEG, int x, int y, int iOptions);
int JPEG_decodeDither(JPEGIMAGE *pJPEG, uint8_t *pDither, int iOptions);
int JPEG_getLastError(JPEGIMAGE *pJPEG);
void JPEG_close(JPEGIMAGE *pJPEG);
}

void paper_touch_set_rotation(uint_fast8_t rot);

namespace {

constexpr const char *kTag = "display_fastepd";

struct SystemBbfFont {
    int32_t size;
    const uint8_t *ptr;
};

constexpr SystemBbfFont kInterMediumBbfFonts[] = {
    {8, _binary_inter_medium_8_bbf},
    {9, _binary_inter_medium_9_bbf},
    {10, _binary_inter_medium_10_bbf},
    {11, _binary_inter_medium_11_bbf},
    {12, _binary_inter_medium_12_bbf},
    {13, _binary_inter_medium_13_bbf},
    {14, _binary_inter_medium_14_bbf},
    {15, _binary_inter_medium_15_bbf},
    {16, _binary_inter_medium_16_bbf},
    {18, _binary_inter_medium_18_bbf},
    {20, _binary_inter_medium_20_bbf},
    {22, _binary_inter_medium_22_bbf},
    {24, _binary_inter_medium_24_bbf},
    {26, _binary_inter_medium_26_bbf},
    {28, _binary_inter_medium_28_bbf},
    {30, _binary_inter_medium_30_bbf},
    {32, _binary_inter_medium_32_bbf},
};

const uint8_t *pick_closest_system_bbf_font(const SystemBbfFont *fonts, size_t count, int32_t want_size,
    int32_t *out_selected_size)
{
    if (!fonts || count == 0) {
        return nullptr;
    }

    size_t best_index = 0;
    uint64_t best_diff = UINT64_MAX;
    for (size_t i = 0; i < count; ++i) {
        const int64_t diff = (int64_t)want_size - (int64_t)fonts[i].size;
        const uint64_t abs_diff = (diff < 0) ? (uint64_t)(-diff) : (uint64_t)diff;
        if (abs_diff < best_diff || (abs_diff == best_diff && fonts[i].size < fonts[best_index].size)) {
            best_index = i;
            best_diff = abs_diff;
        }
    }

    if (out_selected_size) {
        *out_selected_size = fonts[best_index].size;
    }
    return fonts[best_index].ptr;
}

constexpr size_t kMaxJpgBytes = 1024 * 1024;
constexpr size_t kMaxPngBytes = 1024 * 1024;
constexpr size_t kMaxXthBytes = 1024 * 1024;
constexpr size_t kMaxXtgBytes = 1024 * 1024;

extern const uint8_t _binary_sleepimage_jpg_start[] asm("_binary_sleepimage_jpg_start");
extern const uint8_t _binary_sleepimage_jpg_end[] asm("_binary_sleepimage_jpg_end");

static FASTEPD g_epd;
static bool g_epd_inited = false;
static uint8_t g_brightness = 0;

uint8_t rgb888_to_gray8(int32_t rgb888)
{
    const uint32_t raw = (uint32_t)rgb888;
    const uint8_t r = (uint8_t)((raw >> 16) & 0xFF);
    const uint8_t g = (uint8_t)((raw >> 8) & 0xFF);
    const uint8_t b = (uint8_t)(raw & 0xFF);
    return (uint8_t)((r * 77u + g * 150u + b * 29u + 128u) >> 8);
}

uint8_t gray8_to_epd_color(uint8_t gray, int32_t mode)
{
    if (mode == BB_MODE_1BPP) {
        return (gray >= 128) ? (uint8_t)BBEP_WHITE : (uint8_t)BBEP_BLACK;
    }
    uint8_t v = (uint8_t)((gray + 8u) >> 4); // nominally 0..15 (but 255 rounds to 16)
    if (v > 15) {
        v = 15;
    }
    return v;
}

bool ensure_epd_ready(void)
{
    if (g_epd_inited) {
        if (g_epd.currentBuffer()) {
            return true;
        }
        ESP_LOGW(kTag, "FastEPD marked inited but framebuffer missing; forcing reinit");
        g_epd.deInit();
        bbepDeinitBus();
        g_epd_inited = false;
    }

    if (!g_epd_inited) {
        hold_pwroff_pulse_low();
        const int rc = g_epd.initPanel(BB_PANEL_M5PAPERS3);
        if (rc != BBEP_SUCCESS) {
            ESP_LOGW(kTag, "FastEPD initPanel failed (%d)", rc);
            return false;
        }
        (void)g_epd.setMode(BB_MODE_4BPP);
        (void)g_epd.setRotation(90);
        g_epd.fillScreen(0xF);
        const int update_rc = g_epd.fullUpdate(CLEAR_FAST, false);
        if (update_rc != BBEP_SUCCESS) {
            ESP_LOGW(kTag, "FastEPD initial clear fullUpdate failed (%d)", update_rc);
            return false;
        }
        g_epd.backupPlane();
        g_epd_inited = true;
    }
    return g_epd.currentBuffer() != nullptr;
}

int32_t require_epd_ready_or_set_error(const char *context)
{
    if (ensure_epd_ready()) {
        return kWasmOk;
    }
    wasm_api_set_last_error(kWasmErrNotReady, context);
    return kWasmErrNotReady;
}

} // namespace

int32_t display_fastepd_full_update_slow()
{
    const int32_t ready_rc = require_epd_ready_or_set_error("full_update_slow: display not ready");
    if (ready_rc != kWasmOk) {
        return ready_rc;
    }
    const int epd_rc = g_epd.fullUpdate(CLEAR_SLOW, false);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "full_update_slow: FastEPD fullUpdate failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

namespace {

struct JpegDrawContext {
    FASTEPD *epd;
    int32_t clip_x0;
    int32_t clip_y0;
    int32_t clip_x1;
    int32_t clip_y1;
    int32_t mode;
};

int epd_jpeg_draw(JPEGDRAW *pDraw)
{
    if (!pDraw || !pDraw->pPixels) {
        return 0;
    }

    auto *ctx = (JpegDrawContext *)pDraw->pUser;
    if (!ctx || !ctx->epd) {
        return 0;
    }

    const int epd_w = ctx->epd->width();
    const int epd_h = ctx->epd->height();
    if (epd_w <= 0 || epd_h <= 0) {
        return 0;
    }

    const int32_t clip_x0 = ctx->clip_x0 < 0 ? 0 : (ctx->clip_x0 > epd_w ? epd_w : ctx->clip_x0);
    const int32_t clip_y0 = ctx->clip_y0 < 0 ? 0 : (ctx->clip_y0 > epd_h ? epd_h : ctx->clip_y0);
    const int32_t clip_x1 = ctx->clip_x1 < 0 ? 0 : (ctx->clip_x1 > epd_w ? epd_w : ctx->clip_x1);
    const int32_t clip_y1 = ctx->clip_y1 < 0 ? 0 : (ctx->clip_y1 > epd_h ? epd_h : ctx->clip_y1);
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) {
        return 1;
    }

    const int src_block_w = pDraw->iWidthUsed > 0 ? pDraw->iWidthUsed : pDraw->iWidth;
    const int src_block_h = pDraw->iHeight;
    if (src_block_w <= 0 || src_block_h <= 0) {
        return 1;
    }

    const int32_t dst_x0 = pDraw->x;
    const int32_t dst_y0 = pDraw->y;

    const int32_t dst_x1 = dst_x0 + src_block_w;
    const int32_t dst_y1 = dst_y0 + src_block_h;

    const int32_t draw_x0 = dst_x0 > clip_x0 ? dst_x0 : clip_x0;
    const int32_t draw_y0 = dst_y0 > clip_y0 ? dst_y0 : clip_y0;
    const int32_t draw_x1 = dst_x1 < clip_x1 ? dst_x1 : clip_x1;
    const int32_t draw_y1 = dst_y1 < clip_y1 ? dst_y1 : clip_y1;
    if (draw_x0 >= draw_x1 || draw_y0 >= draw_y1) {
        return 1;
    }

    const int32_t src_x0 = draw_x0 - dst_x0;
    const int32_t src_y0 = draw_y0 - dst_y0;
    const int32_t copy_w = draw_x1 - draw_x0;
    const int32_t copy_h = draw_y1 - draw_y0;

    if (pDraw->iBpp == 4) {
        const uint8_t *src = (const uint8_t *)pDraw->pPixels;
        const int src_pitch = (pDraw->iWidth + 1) / 2;
        for (int yy = 0; yy < copy_h; ++yy) {
            const uint8_t *src_row = &src[(src_y0 + yy) * src_pitch];
            const int32_t dy = draw_y0 + yy;
            for (int xx = 0; xx < copy_w; ++xx) {
                const int32_t sx = src_x0 + xx;
                const uint8_t packed = src_row[sx / 2];
                const uint8_t v4 = (uint8_t)((sx & 1) ? (packed & 0x0F) : (packed >> 4));
                const int32_t dx = draw_x0 + xx;
                if (ctx->mode == BB_MODE_1BPP) {
                    ctx->epd->drawPixelFast(dx, dy, v4 >= 8 ? BBEP_WHITE : BBEP_BLACK);
                } else {
                    ctx->epd->drawPixelFast(dx, dy, v4);
                }
            }
        }
    } else {
        const uint8_t *src = (const uint8_t *)pDraw->pPixels;
        for (int yy = 0; yy < copy_h; ++yy) {
            const uint8_t *src_row = &src[(src_y0 + yy) * pDraw->iWidth + src_x0];
            const int32_t dy = draw_y0 + yy;
            for (int xx = 0; xx < copy_w; ++xx) {
                const uint8_t v4 = (uint8_t)(src_row[xx] >> 4);
                const int32_t dx = draw_x0 + xx;
                if (ctx->mode == BB_MODE_1BPP) {
                    ctx->epd->drawPixelFast(dx, dy, v4 >= 8 ? BBEP_WHITE : BBEP_BLACK);
                } else {
                    ctx->epd->drawPixelFast(dx, dy, v4);
                }
            }
        }
    }

    return 1;
}

	int32_t draw_jpg_internal(
	    const uint8_t *ptr,
	    size_t len,
	    int32_t x,
	    int32_t y,
	    int32_t max_w,
	    int32_t max_h,
	    bool do_fit)
	{
    if (x < 0 || y < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg: negative coordinates");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxJpgBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg: len too large");
        return kWasmErrInvalidArgument;
    }
    if (do_fit && (max_w < 0 || max_h < 0)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: negative max_w/max_h");
        return kWasmErrInvalidArgument;
    }
    if (do_fit && (max_w == 0 || max_h == 0)) {
        return kWasmOk;
    }
    const int32_t ready_rc = require_epd_ready_or_set_error("draw_jpg: framebuffer not ready");
    if (ready_rc != kWasmOk) {
        return ready_rc;
    }

    const int32_t mode = g_epd.getMode();
    if (mode != BB_MODE_1BPP && mode != BB_MODE_4BPP) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg: unsupported mode (expected 1-bpp or 4-bpp)");
        return kWasmErrInvalidArgument;
    }

    JpegDrawContext ctx = {};
    ctx.epd = &g_epd;
    ctx.mode = mode;

	    if (do_fit) {
	        ctx.clip_x0 = x;
	        ctx.clip_y0 = y;
	        ctx.clip_x1 = x + max_w;
	        ctx.clip_y1 = y + max_h;
	    } else {
	        ctx.clip_x0 = 0;
	        ctx.clip_y0 = 0;
	        ctx.clip_x1 = g_epd.width();
	        ctx.clip_y1 = g_epd.height();
	    }

	    JPEGIMAGE *jpeg = (JPEGIMAGE *)calloc(1, sizeof(JPEGIMAGE));
	    if (!jpeg) {
	        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg: out of memory");
	        return kWasmErrInternal;
	    }

	    const int opened = JPEG_openRAM(jpeg, (uint8_t *)ptr, (int)len, epd_jpeg_draw);
	    if (!opened) {
	        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg: JPEG openRAM failed");
	        free(jpeg);
	        return kWasmErrInternal;
	    }
	    jpeg->pUser = &ctx;

	    int options = 0;
	    if (do_fit) {
	        const int img_w = JPEG_getWidth(jpeg);
	        const int img_h = JPEG_getHeight(jpeg);
	        int scale = 1;
	        if (img_w > 0 && img_h > 0) {
	            const int w2 = (img_w + 1) / 2;
	            const int h2 = (img_h + 1) / 2;
	            const int w4 = (img_w + 3) / 4;
            const int h4 = (img_h + 3) / 4
;
            if (img_w <= max_w && img_h <= max_h) {
                scale = 1;
            } else if (w2 <= max_w && h2 <= max_h) {
                scale = 2;
            } else if (w4 <= max_w && h4 <= max_h) {
                scale = 4;
            } else {
                scale = 8;
            }
        }
        if (scale == 2) {
            options |= JPEG_SCALE_HALF;
        } else if (scale == 4) {
            options |= JPEG_SCALE_QUARTER;
        } else if (scale == 8) {
            options |= JPEG_SCALE_EIGHTH;
	        }
	    }

	    const int subsample = JPEG_getSubSample(jpeg);
	    int base_mcu_w = 8;
	    int base_mcu_h = 8;
	    switch (subsample) {
	        case 0x12:
            base_mcu_w = 8;
            base_mcu_h = 16;
            break;
        case 0x21:
            base_mcu_w = 16;
            base_mcu_h = 8;
            break;
        case 0x22:
            base_mcu_w = 16;
            base_mcu_h = 16;
            break;
        default:
            base_mcu_w = 8;
            base_mcu_h = 8;
            break;
    }

    int scale_shift = 0;
    if (options & JPEG_SCALE_HALF) {
        scale_shift = 1;
    } else if (options & JPEG_SCALE_QUARTER) {
        scale_shift = 2;
    } else if (options & JPEG_SCALE_EIGHTH) {
        scale_shift = 3;
	    }
	    const int mcu_w = base_mcu_w >> scale_shift;
	    const int mcu_h = base_mcu_h >> scale_shift;

	    const int img_w = JPEG_getWidth(jpeg);
	    const int cx = base_mcu_w == 16 ? ((img_w + 15) >> 4) : ((img_w + 7) >> 3);
	    const size_t aligned_w = (size_t)cx * (size_t)mcu_w;
	    const size_t dither_buf_len = aligned_w * (size_t)mcu_h;

	    bool ok = false;
	    uint8_t *dither_buf = nullptr;
	    if (dither_buf_len > 0) {
	        dither_buf = (uint8_t *)malloc(dither_buf_len);
	    }

	    if (dither_buf) {
	        JPEG_setPixelType(jpeg, FOUR_BIT_DITHERED);
	        jpeg->iXOffset = x;
	        jpeg->iYOffset = y;
	        ok = JPEG_decodeDither(jpeg, dither_buf, options) != 0;
	        free(dither_buf);
	    } else {
	        JPEG_setPixelType(jpeg, EIGHT_BIT_GRAYSCALE);
	        ok = JPEG_decode(jpeg, x, y, options) != 0;
	    }

	    const int last_err = JPEG_getLastError(jpeg);
	    JPEG_close(jpeg);
	    free(jpeg);

	    if (!ok) {
	        (void)last_err;
	        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg: decode failed");
	        return kWasmErrInternal;
    }

    return kWasmOk;
}

struct PngMemStream {
    const uint8_t *data;
    size_t len;
    size_t pos;
};

struct PngDitherState {
    FASTEPD *epd;
    int32_t dst_x;
    int32_t dst_y;
    int32_t max_w;
    int32_t max_h;
    int32_t current_y;
    int32_t mode;
    int32_t *err_cur;
    int32_t *err_next;
};

struct PngContext {
    PngMemStream stream;
    PngDitherState dither;
};

uint32_t epd_png_read(void *user_data, uint8_t *buf, uint32_t len)
{
    auto *ctx = (PngContext *)user_data;
    if (!ctx) {
        return 0;
    }

    PngMemStream *s = &ctx->stream;
    if (!s || !s->data) {
        return 0;
    }
    if (s->pos >= s->len) {
        return 0;
    }
    const size_t remaining = s->len - s->pos;
    size_t n = (size_t)len;
    if (n > remaining) {
        n = remaining;
    }
    if (buf) {
        memcpy(buf, s->data + s->pos, n);
    }
    s->pos += n;
    return (uint32_t)n;
}

static inline int32_t dither_mul_div16(int32_t v, int32_t mul)
{
    int32_t t = v * mul;
    t += (t >= 0) ? 8 : -8;
    return t >> 4;
}

void epd_png_draw(void *user_data, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t len, const uint8_t *argb)
{
    auto *ctx = (PngContext *)user_data;
    if (!ctx) {
        return;
    }

    PngDitherState *st = &ctx->dither;
    if (!st || !st->epd || !st->err_cur || !st->err_next || !argb) {
        return;
    }
    if ((int32_t)y < 0 || (int32_t)y >= st->max_h) {
        return;
    }
    if (div_x == 0) {
        return;
    }

    if (st->current_y < 0) {
        st->current_y = (int32_t)y;
    }
    if ((int32_t)y != st->current_y) {
        if ((int32_t)y < st->current_y) {
            memset(st->err_cur, 0, (size_t)(st->max_w + 3) * sizeof(int32_t));
            memset(st->err_next, 0, (size_t)(st->max_w + 3) * sizeof(int32_t));
            st->current_y = (int32_t)y;
        } else {
            while (st->current_y < (int32_t)y) {
                int32_t *tmp = st->err_cur;
                st->err_cur = st->err_next;
                st->err_next = tmp;
                memset(st->err_next, 0, (size_t)(st->max_w + 3) * sizeof(int32_t));
                st->current_y++;
            }
        }
    }

    const int32_t epd_w = st->epd->width();
    const int32_t epd_h = st->epd->height();
    if (epd_w <= 0 || epd_h <= 0) {
        return;
    }

    uint32_t xi = x;
    for (size_t i = 0; i < len; ++i) {
        if ((int32_t)xi >= 0 && (int32_t)xi < st->max_w) {
            uint8_t a = argb[0];
            uint8_t r = argb[1];
            uint8_t g = argb[2];
            uint8_t b = argb[3];

            if (a != 255) {
                const uint16_t inv = (uint16_t)(255u - (uint16_t)a);
                r = (uint8_t)(((uint16_t)r * a + inv * 255u + 127u) / 255u);
                g = (uint8_t)(((uint16_t)g * a + inv * 255u + 127u) / 255u);
                b = (uint8_t)(((uint16_t)b * a + inv * 255u + 127u) / 255u);
            }

            const int32_t gray = (int32_t)((r * 77u + g * 150u + b * 29u + 128u) >> 8);
            const int idx = (int)xi + 1;

            int32_t v = gray + st->err_cur[idx];
            if (v < 0) v = 0;
            if (v > 255) v = 255;

            const int32_t dx = st->dst_x + (int32_t)xi;
            const int32_t dy = st->dst_y + (int32_t)y;
            if (dx >= 0 && dy >= 0 && dx < epd_w && dy < epd_h) {
                if (st->mode == BB_MODE_1BPP) {
                    const int32_t q = (v >= 128) ? 255 : 0;
                    st->epd->drawPixelFast(dx, dy, (uint8_t)(q ? BBEP_WHITE : BBEP_BLACK));
                    const int32_t err = v - q;
                    st->err_cur[idx + 1] += dither_mul_div16(err, 7);
                    st->err_next[idx - 1] += dither_mul_div16(err, 3);
                    st->err_next[idx] += dither_mul_div16(err, 5);
                    st->err_next[idx + 1] += dither_mul_div16(err, 1);
                } else {
                    int32_t q = (v + 8) >> 4;
                    if (q < 0) q = 0;
                    if (q > 15) q = 15;
                    st->epd->drawPixelFast(dx, dy, (uint8_t)q);

                    const int32_t recon = q * 17;
                    const int32_t err = v - recon;
                    st->err_cur[idx + 1] += dither_mul_div16(err, 7);
                    st->err_next[idx - 1] += dither_mul_div16(err, 3);
                    st->err_next[idx] += dither_mul_div16(err, 5);
                    st->err_next[idx + 1] += dither_mul_div16(err, 1);
                }
            }
        }

        argb += 4;
        xi += (uint32_t)div_x;
    }
}

int32_t draw_png_internal(
    const uint8_t *ptr,
    size_t len,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h,
    bool do_fit)
{
    if (x < 0 || y < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: negative coordinates");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxPngBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: len too large");
        return kWasmErrInvalidArgument;
    }
    if (do_fit && (max_w < 0 || max_h < 0)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: negative max_w/max_h");
        return kWasmErrInvalidArgument;
    }
    if (do_fit && (max_w == 0 || max_h == 0)) {
        return kWasmOk;
    }
    const int32_t ready_rc = require_epd_ready_or_set_error("draw_png: framebuffer not ready");
    if (ready_rc != kWasmOk) {
        return ready_rc;
    }

    const int32_t mode = g_epd.getMode();
    if (mode != BB_MODE_1BPP && mode != BB_MODE_4BPP) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: unsupported mode (expected 1-bpp or 4-bpp)");
        return kWasmErrInvalidArgument;
    }

    pngle_t *pngle = lgfx_pngle_new();
    if (!pngle) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png: pngle alloc failed");
        return kWasmErrInternal;
    }

    PngContext ctx = {};
    ctx.stream.data = ptr;
    ctx.stream.len = len;
    ctx.stream.pos = 0;

    if (lgfx_pngle_prepare(pngle, epd_png_read, &ctx) < 0) {
        lgfx_pngle_destroy(pngle);
        wasm_api_set_last_error(kWasmErrInternal, "draw_png: pngle prepare failed");
        return kWasmErrInternal;
    }

    const int32_t img_w = (int32_t)lgfx_pngle_get_width(pngle);
    const int32_t img_h = (int32_t)lgfx_pngle_get_height(pngle);
    if (img_w <= 0 || img_h <= 0) {
        lgfx_pngle_destroy(pngle);
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: invalid image dims");
        return kWasmErrInvalidArgument;
    }

    const int32_t epd_w = g_epd.width();
    const int32_t epd_h = g_epd.height();
    if (epd_w <= 0 || epd_h <= 0) {
        lgfx_pngle_destroy(pngle);
        wasm_api_set_last_error(kWasmErrNotReady, "draw_png: display not initialized");
        return kWasmErrNotReady;
    }

    int32_t draw_w = img_w;
    int32_t draw_h = img_h;
    if (do_fit) {
        if (draw_w > max_w) draw_w = max_w;
        if (draw_h > max_h) draw_h = max_h;
    }
    const int32_t avail_w = epd_w - x;
    const int32_t avail_h = epd_h - y;
    if (draw_w > avail_w) draw_w = avail_w;
    if (draw_h > avail_h) draw_h = avail_h;
    if (draw_w <= 0 || draw_h <= 0) {
        lgfx_pngle_destroy(pngle);
        return kWasmOk;
    }

    ctx.dither.epd = &g_epd;
    ctx.dither.dst_x = x;
    ctx.dither.dst_y = y;
    ctx.dither.max_w = draw_w;
    ctx.dither.max_h = draw_h;
    ctx.dither.current_y = -1;
    ctx.dither.mode = mode;
    ctx.dither.err_cur = (int32_t *)calloc((size_t)(draw_w + 3), sizeof(int32_t));
    ctx.dither.err_next = (int32_t *)calloc((size_t)(draw_w + 3), sizeof(int32_t));
    if (!ctx.dither.err_cur || !ctx.dither.err_next) {
        if (ctx.dither.err_cur) free(ctx.dither.err_cur);
        if (ctx.dither.err_next) free(ctx.dither.err_next);
        lgfx_pngle_destroy(pngle);
        wasm_api_set_last_error(kWasmErrInternal, "draw_png: dither buffers alloc failed");
        return kWasmErrInternal;
    }

    const int png_rc = lgfx_pngle_decomp(pngle, epd_png_draw);

    free(ctx.dither.err_cur);
    free(ctx.dither.err_next);
    lgfx_pngle_destroy(pngle);

    if (png_rc < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png: decode failed");
        return kWasmErrInternal;
    }

    return kWasmOk;
}

bool read_file_all(const char *path, uint8_t **out_buf, size_t *out_len, size_t max_len)
{
    if (!out_buf || !out_len) {
        return false;
    }
    *out_buf = nullptr;
    *out_len = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    const long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return false;
    }
    if ((size_t)size > max_len) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return false;
    }

    const size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(buf);
        return false;
    }

    *out_buf = buf;
    *out_len = n;
    return true;
}

void warn_unimplemented(const char *name)
{
    ESP_LOGW(kTag, "[unimplemented] %s called", name);
}

int32_t filled_triangle(
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    uint8_t color)
{
    if (y0 > y1) { int32_t t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int32_t t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int32_t t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    if (y0 == y2) {
        int32_t min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        int32_t max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        g_epd.drawLine(min_x, y0, max_x, y0, color);
        return kWasmOk;
    }

    const int32_t total_h = y2 - y0;
    for (int32_t i = 0; i <= total_h; ++i) {
        const bool second_half = i > (y1 - y0) || y1 == y0;
        const int32_t segment_h = second_half ? (y2 - y1) : (y1 - y0);
        const int32_t ay = y0 + i;
        const float alpha = (float)i / (float)total_h;
        const float beta = segment_h == 0 ? 0.0f
            : (float)(i - (second_half ? (y1 - y0) : 0)) / (float)segment_h;
        const int32_t ax = x0 + (int32_t)((x2 - x0) * alpha);
        const int32_t bx = second_half
            ? (x1 + (int32_t)((x2 - x1) * beta))
            : (x0 + (int32_t)((x1 - x0) * beta));
        const int32_t x_start = ax < bx ? ax : bx;
        const int32_t x_end = ax > bx ? ax : bx;
        g_epd.drawLine(x_start, ay, x_end, ay, color);
    }
    return kWasmOk;
}

void draw_ellipse_outline(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint8_t color)
{
    int64_t x = 0;
    int64_t y = ry;
    const int64_t rx2 = (int64_t)rx * rx;
    const int64_t ry2 = (int64_t)ry * ry;
    const int64_t two_rx2 = 2 * rx2;
    const int64_t two_ry2 = 2 * ry2;

    int64_t px = 0;
    int64_t py = two_rx2 * y;
    int64_t p = ry2 - (rx2 * y) + (rx2 / 4);

    auto plot4 = [&](int64_t px0, int64_t py0) {
        g_epd.drawPixel(cx + (int32_t)px0, cy + (int32_t)py0, color);
        g_epd.drawPixel(cx - (int32_t)px0, cy + (int32_t)py0, color);
        g_epd.drawPixel(cx + (int32_t)px0, cy - (int32_t)py0, color);
        g_epd.drawPixel(cx - (int32_t)px0, cy - (int32_t)py0, color);
    };

    plot4(x, y);
    while (px < py) {
        ++x;
        px += two_ry2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            --y;
            py -= two_rx2;
            p += ry2 + px - py;
        }
        plot4(x, y);
    }

    p = ry2 * (x * x + x) + (ry2 / 4) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y > 0) {
        --y;
        py -= two_rx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            ++x;
            px += two_ry2;
            p += rx2 - py + px;
        }
        plot4(x, y);
    }
}

void fill_ellipse_scanlines(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint8_t color)
{
    int64_t x = 0;
    int64_t y = ry;
    const int64_t rx2 = (int64_t)rx * rx;
    const int64_t ry2 = (int64_t)ry * ry;
    const int64_t two_rx2 = 2 * rx2;
    const int64_t two_ry2 = 2 * ry2;

    int64_t px = 0;
    int64_t py = two_rx2 * y;
    int64_t p = ry2 - (rx2 * y) + (rx2 / 4);

    auto draw_pair = [&](int64_t px0, int64_t py0) {
        g_epd.drawLine(cx - (int32_t)px0, cy + (int32_t)py0, cx + (int32_t)px0, cy + (int32_t)py0, color);
        if (py0 != 0) {
            g_epd.drawLine(cx - (int32_t)px0, cy - (int32_t)py0, cx + (int32_t)px0, cy - (int32_t)py0, color);
        }
    };

    draw_pair(x, y);
    while (px < py) {
        ++x;
        px += two_ry2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            --y;
            py -= two_rx2;
            p += ry2 + px - py;
        }
        draw_pair(x, y);
    }

    p = ry2 * (x * x + x) + (ry2 / 4) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y > 0) {
        --y;
        py -= two_rx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            ++x;
            px += two_ry2;
            p += rx2 - py + px;
        }
        draw_pair(x, y);
    }
}

} // namespace

PaperDisplayDriver DisplayFastEpd::driver() {
    return PaperDisplayDriver::fastepd;
}

bool DisplayFastEpd::init()
{
    if (g_epd_inited && g_epd.currentBuffer() != nullptr) {
        return true;
    }

    ESP_LOGI(kTag, "Initializing FastEPD display...");
    if (!ensure_epd_ready()) {
        ESP_LOGE(kTag, "FastEPD initialization failed");
        return false;
    }

    ESP_LOGI(kTag, "FastEPD init OK: w=%d h=%d mode=%d rotation=%d",
             g_epd.width(),
             g_epd.height(),
             g_epd.getMode(),
             g_epd.getRotation());
    return true;
}

int32_t DisplayFastEpd::release(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    ESP_LOGI(kTag, "release: deinitializing FastEPD resources");
    g_epd.deInit();
    bbepDeinitBus();
    g_epd_inited = false;
    ESP_LOGI(kTag, "release: FastEPD deinitialized (bus + panel io released)");
    return kWasmOk;
}

int32_t DisplayFastEpd::width(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("width: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    return (int32_t)g_epd.width();
}

int32_t DisplayFastEpd::height(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("height: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    return (int32_t)g_epd.height();
}

int32_t DisplayFastEpd::getRotation(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("getRotation: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int rot_deg = g_epd.getRotation();
    switch (rot_deg) {
        case 0:
            return 0;
        case 90:
            return 1;
        case 180:
            return 2;
        case 270:
            return 3;
        default:
            ESP_LOGW(kTag, "Unexpected FastEPD rotation degrees=%d", rot_deg);
            return 0;
    }
}

int32_t DisplayFastEpd::setRotation(wasm_exec_env_t exec_env, int32_t rot)
{
    (void)exec_env;
    if (rot < 0 || rot > 3) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setRotation: rot out of range (expected 0..3)");
        return kWasmErrInvalidArgument;
    }
    const int32_t rc = require_epd_ready_or_set_error("setRotation: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int degrees = rot * 90;
    const int epd_rc = g_epd.setRotation(degrees);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "setRotation: FastEPD setRotation failed");
        return kWasmErrInternal;
    }
    // Keep LGFX touch coordinate conversion aligned with active FastEPD rotation.
    // FastEPD defaults to 90deg while LGFX touch baseline is rot=0 on this board.
    const uint_fast8_t lgfx_rot = (uint_fast8_t)((rot + 3) & 0x3);
    paper_touch_set_rotation(lgfx_rot);
    return kWasmOk;
}

int32_t DisplayFastEpd::clear(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("clear: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    g_epd.fillScreen(mode == BB_MODE_1BPP ? (uint8_t)BBEP_WHITE : (uint8_t)0xF);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillScreen(wasm_exec_env_t exec_env, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillScreen: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t gray = rgb888_to_gray8(rgb888);
    g_epd.fillScreen(gray8_to_epd_color(gray, mode));
    return kWasmOk;
}

int32_t DisplayFastEpd::display(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("display: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int epd_rc = g_epd.fullUpdate(CLEAR_SLOW, false);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "display: FastEPD fullUpdate failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayFastEpd::displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("displayRect: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "displayRect: negative argument");
        return kWasmErrInvalidArgument;
    }
    const int32_t max_w = (int32_t)g_epd.width();
    const int32_t max_h = (int32_t)g_epd.height();
    const int64_t x2 = (int64_t)x + (int64_t)w;
    const int64_t y2 = (int64_t)y + (int64_t)h;
    if (x2 > max_w || y2 > max_h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "displayRect: rect out of bounds");
        return kWasmErrInvalidArgument;
    }

    BB_RECT rect = {.x = x, .y = y, .w = w, .h = h};
    const int epd_rc = g_epd.fullUpdate(CLEAR_NONE, false, &rect);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "displayRect: FastEPD fullUpdate failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayFastEpd::fullUpdateSlow(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return display_fastepd_full_update_slow();
}

int32_t DisplayFastEpd::waitDisplay(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    // FastEPD updates are synchronous today.
    warn_unimplemented("waitDisplay");
    return kWasmOk;
}

int32_t DisplayFastEpd::startWrite(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    warn_unimplemented("startWrite");
    return kWasmOk;
}

int32_t DisplayFastEpd::endWrite(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    warn_unimplemented("endWrite");
    return kWasmOk;
}

int32_t DisplayFastEpd::setBrightness(wasm_exec_env_t exec_env, int32_t v)
{
    (void)exec_env;
    if (v < 0 || v > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setBrightness: v out of range (expected 0..255)");
        return kWasmErrInvalidArgument;
    }
    g_brightness = (uint8_t)v;
    g_epd.setBrightness(g_brightness, g_brightness);
    return kWasmOk;
}

int32_t DisplayFastEpd::getBrightness(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)g_brightness;
}

int32_t DisplayFastEpd::setEpdMode(wasm_exec_env_t exec_env, int32_t mode)
{
    warn_unimplemented("setEpdMode");
    return kWasmOk;
}

int32_t DisplayFastEpd::getEpdMode(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("getEpdMode: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    return g_epd.getMode() == BB_MODE_1BPP ? 1 : 2;
}

int32_t DisplayFastEpd::setCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("setCursor: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    g_epd.setCursor(x, y);
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextSize(wasm_exec_env_t exec_env, float sx, float sy)
{
    (void)exec_env;
    (void)sx;
    (void)sy;
    warn_unimplemented("setTextSize");
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextDatum(wasm_exec_env_t exec_env, int32_t datum)
{
    (void)exec_env;
    (void)datum;
    warn_unimplemented("setTextDatum");
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextColor(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("setTextColor: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t fg = gray8_to_epd_color(rgb888_to_gray8(fg_rgb888), mode);
    const int bg = use_bg ? (int)gray8_to_epd_color(rgb888_to_gray8(bg_rgb888), mode) : BBEP_TRANSPARENT;
    g_epd.setTextColor((int)fg, bg);
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextWrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("setTextWrap: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    g_epd.setTextWrap((wrap_x != 0) || (wrap_y != 0));
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextScroll(wasm_exec_env_t exec_env, int32_t scroll)
{
    (void)exec_env;
    (void)scroll;
    warn_unimplemented("setTextScroll");
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextFont(wasm_exec_env_t exec_env, int32_t font_id)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("setTextFont: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (font_id < 0 || font_id >= FONT_COUNT) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setTextFont: font_id out of range (expected 0..3)");
        return kWasmErrInvalidArgument;
    }
    g_epd.setFont(font_id);
    return kWasmOk;
}

int32_t DisplayFastEpd::setTextEncoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable)
{
    (void)exec_env;
    (void)utf8_enable;
    (void)cp437_enable;
    warn_unimplemented("setTextEncoding");
    return kWasmOk;
}

int32_t DisplayFastEpd::drawString(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawString: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (!s) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawString: s is null");
        return kWasmErrInvalidArgument;
    }
    BB_RECT rect;
    g_epd.setCursor(0, 0);
    if (BBEP_SUCCESS == g_epd.getStringBox(s, &rect)) {
        y -= rect.y;
    }
    g_epd.drawString(s, x, y);
    return kWasmOk;
}

int32_t DisplayFastEpd::textWidth(wasm_exec_env_t exec_env, const char *s)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("textWidth: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (!s) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "textWidth: s is null");
        return kWasmErrInvalidArgument;
    }
    BB_RECT rect = {};
    const int epd_rc = g_epd.getStringBox(s, &rect);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "textWidth: getStringBox failed");
        return kWasmErrInternal;
    }
    return rect.w;
}

int32_t DisplayFastEpd::fontHeight(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fontHeight: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    BB_RECT rect = {};
    const int epd_rc = g_epd.getStringBox("M", &rect);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "fontHeight: getStringBox failed");
        return kWasmErrInternal;
    }
    return rect.h;
}

int32_t DisplayFastEpd::vlwRegister(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    (void)ptr;
    (void)len;
    warn_unimplemented("vlwRegister");
    return kWasmOk;
}

int32_t DisplayFastEpd::vlwUse(wasm_exec_env_t exec_env, int32_t handle)
{
    warn_unimplemented("vlwUse");
    return kWasmOk;
}

int32_t DisplayFastEpd::vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id, int32_t font_size)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("vlwUseSystem: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (font_size <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwUseSystem: invalid font_size");
        return kWasmErrInvalidArgument;
    }

    switch (font_id) {
    case kVlwSystemFontInter: {
        int32_t selected_size = 0;
        const uint8_t *font_ptr = pick_closest_system_bbf_font(kInterMediumBbfFonts,
            sizeof(kInterMediumBbfFonts) / sizeof(kInterMediumBbfFonts[0]), font_size, &selected_size);
        if (!font_ptr) {
            wasm_api_set_last_error(kWasmErrInternal, "vlwUseSystem: no fonts available");
            return kWasmErrInternal;
        }
        g_epd.setFont(font_ptr, false);
        ESP_LOGI(kTag, "vlwUseSystem loaded inter_medium_%" PRId32 " (requested=%" PRId32 ")", selected_size,
            font_size);
        break;
    }
    default:
        ESP_LOGI(kTag, "vlwUseSystem rejected invalid font_id=%" PRId32, font_id);
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwUseSystem: invalid font_id");
        return kWasmErrInvalidArgument;
    }
    return kWasmOk;
}

int32_t DisplayFastEpd::vlwUnload(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    warn_unimplemented("vlwUnload");
    return kWasmOk;
}

int32_t DisplayFastEpd::vlwClearAll(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    warn_unimplemented("vlwClearAll");
    return kWasmOk;
}

int32_t DisplayFastEpd::pushImageRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    (void)exec_env;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)ptr;
    (void)len;
    warn_unimplemented("pushImageRgb565");
    return kWasmOk;
}

int32_t DisplayFastEpd::pushImage(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *data_ptr,
    size_t data_len,
    int32_t depth_raw,
    const uint8_t *palette_ptr,
    size_t palette_len)
{
    (void)exec_env;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)data_ptr;
    (void)data_len;
    (void)depth_raw;
    (void)palette_ptr;
    (void)palette_len;
    warn_unimplemented("pushImage");
    return kWasmOk;
}

int32_t DisplayFastEpd::pushImageGray8(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("pushImageGray8: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "pushImageGray8: negative argument");
        return kWasmErrInvalidArgument;
    }
    const uint64_t pixels = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h;
    if (pixels > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "pushImageGray8: size overflow");
        return kWasmErrInvalidArgument;
    }
    const size_t expected_len = (size_t)pixels;
    if (!ptr && expected_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "pushImageGray8: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len < expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "pushImageGray8: len too small");
        return kWasmErrInvalidArgument;
    }

    const int32_t mode = g_epd.getMode();
    const int32_t epd_w = g_epd.width();
    const int32_t epd_h = g_epd.height();
    for (int32_t yy = 0; yy < h; ++yy) {
        const int32_t dy = y + yy;
        if (dy < 0 || dy >= epd_h) {
            continue;
        }
        const uint8_t *row = ptr + (size_t)yy * (size_t)w;
        for (int32_t xx = 0; xx < w; ++xx) {
            const int32_t dx = x + xx;
            if (dx < 0 || dx >= epd_w) {
                continue;
            }
            g_epd.drawPixelFast(dx, dy, gray8_to_epd_color(row[xx], mode));
        }
    }
    return kWasmOk;
}

int32_t DisplayFastEpd::readRectRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    uint8_t *out,
    size_t out_len)
{
    (void)exec_env;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)out;
    (void)out_len;
    warn_unimplemented("readRectRgb565");
    wasm_api_set_last_error(kWasmErrInternal, "readRectRgb565: not supported by FastEPD");
    return kWasmErrInternal;
}

int32_t DisplayFastEpd::drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    return draw_png_internal(ptr, len, x, y, 0, 0, false);
}

int32_t DisplayFastEpd::drawXth(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxXthBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: len too large");
        return kWasmErrInvalidArgument;
    }
    const int32_t ready_rc = require_epd_ready_or_set_error("draw_xth_centered: framebuffer not ready");
    if (ready_rc != kWasmOk) {
        return ready_rc;
    }

    const int32_t mode = g_epd.getMode();
    if (mode != BB_MODE_1BPP && mode != BB_MODE_4BPP) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: unsupported mode (expected 1-bpp or 4-bpp)");
        return kWasmErrInvalidArgument;
    }

    return 0;
}

int32_t DisplayFastEpd::drawXtg(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxXtgBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: len too large");
        return kWasmErrInvalidArgument;
    }
    const int32_t ready_rc = require_epd_ready_or_set_error("draw_xtg_centered: framebuffer not ready");
    if (ready_rc != kWasmOk) {
        return ready_rc;
    }

    const int32_t mode = g_epd.getMode();
    if (mode != BB_MODE_1BPP && mode != BB_MODE_4BPP) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: unsupported mode (expected 1-bpp or 4-bpp)");
        return kWasmErrInvalidArgument;
    }

    return 0;
}

int32_t DisplayFastEpd::drawJpgFit(
    wasm_exec_env_t exec_env,
    const uint8_t *ptr,
    size_t len,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    return draw_jpg_internal(ptr, len, x, y, max_w, max_h, true);
}

int32_t DisplayFastEpd::drawPngFit(
    wasm_exec_env_t exec_env,
    const uint8_t *ptr,
    size_t len,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    return draw_png_internal(ptr, len, x, y, max_w, max_h, true);
}

int32_t DisplayFastEpd::drawJpgFile(
    wasm_exec_env_t exec_env,
    const char *path,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawJpgFile: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawJpgFile: path is null");
        return kWasmErrInvalidArgument;
    }
    if (max_w == 0 || max_h == 0) {
        return kWasmOk;
    }

    uint8_t *buf = nullptr;
    size_t len = 0;
    if (!read_file_all(path, &buf, &len, kMaxJpgBytes)) {
        wasm_api_set_last_error(kWasmErrNotFound, "drawJpgFile: failed to read file");
        return kWasmErrNotFound;
    }
    const int32_t rc = draw_jpg_internal(buf, len, x, y, max_w, max_h, true);
    free(buf);
    return rc;
}

int32_t DisplayFastEpd::drawPngFile(
    wasm_exec_env_t exec_env,
    const char *path,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawPngFile: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawPngFile: path is null");
        return kWasmErrInvalidArgument;
    }
    if (max_w == 0 || max_h == 0) {
        return kWasmOk;
    }

    uint8_t *buf = nullptr;
    size_t len = 0;
    if (!read_file_all(path, &buf, &len, kMaxPngBytes)) {
        wasm_api_set_last_error(kWasmErrNotFound, "drawPngFile: failed to read file");
        return kWasmErrNotFound;
    }
    const int32_t rc = draw_png_internal(buf, len, x, y, max_w, max_h, true);
    free(buf);
    return rc;
}

int32_t DisplayFastEpd::drawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawPixel: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t w = (int32_t)g_epd.width();
    const int32_t h = (int32_t)g_epd.height();
    if (x < 0 || y < 0 || x >= w || y >= h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawPixel: coordinates out of bounds");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.drawPixel(x, y, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::drawLine(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawLine: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.drawLine(x0, y0, x1, y1, (int)color);
    return kWasmOk;
}

int32_t DisplayFastEpd::drawFastVline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    if (h <= 0) {
        return kWasmOk;
    }
    return drawLine(exec_env, x, y, x, y + h - 1, rgb888);
}

int32_t DisplayFastEpd::drawFastHline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888)
{
    (void)exec_env;
    if (w <= 0) {
        return kWasmOk;
    }
    return drawLine(exec_env, x, y, x + w - 1, y, rgb888);
}

int32_t DisplayFastEpd::drawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawRect: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawRect: negative size");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.drawRect(x, y, w, h, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillRect: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillRect: negative size");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.fillRect(x, y, w, h, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::drawRoundRect(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    int32_t r,
    int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawRoundRect: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawRoundRect: negative size");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.drawRoundRect(x, y, w, h, r, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillRoundRect(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    int32_t r,
    int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillRoundRect: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillRoundRect: negative size");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.fillRoundRect(x, y, w, h, r, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::drawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawCircle: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.drawCircle(x, y, r, (uint32_t)color);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillCircle: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.fillCircle(x, y, r, (uint32_t)color);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillArc(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t r0,
    int32_t r1,
    float angle0,
    float angle1,
    int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillArc: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (r0 < 0 || r1 < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillArc: r0 < 0 or r1 < 0");
        return kWasmErrInvalidArgument;
    }
    if (r1 > r0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillArc: r1 > r0");
        return kWasmErrInvalidArgument;
    }
    if (r0 == r1) {
        return kWasmOk;
    }

    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    display_fastepd_fill_arc(g_epd, x, y, r0, r1, angle0, angle1, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::drawEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawEllipse: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (rx < 0 || ry < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawEllipse: rx < 0 or ry < 0");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    draw_ellipse_outline(x, y, rx, ry, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillEllipse: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (rx < 0 || ry < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillEllipse: rx < 0 or ry < 0");
        return kWasmErrInvalidArgument;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    fill_ellipse_scanlines(x, y, rx, ry, color);
    return kWasmOk;
}

int32_t DisplayFastEpd::drawTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("drawTriangle: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    g_epd.drawLine(x0, y0, x1, y1, (int)color);
    g_epd.drawLine(x1, y1, x2, y2, (int)color);
    g_epd.drawLine(x2, y2, x0, y0, (int)color);
    return kWasmOk;
}

int32_t DisplayFastEpd::fillTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("fillTriangle: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    const int32_t mode = g_epd.getMode();
    const uint8_t color = gray8_to_epd_color(rgb888_to_gray8(rgb888), mode);
    return filled_triangle(x0, y0, x1, y1, x2, y2, color);
}
