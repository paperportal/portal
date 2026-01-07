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

#include "esp_log.h"

#include "../api.h"
#include "errors.h"

extern const uint8_t _binary_inter_medium_32_bbf_start[] asm("_binary_inter_medium_32_bbf_start");
extern const uint8_t _binary_inter_medium_32_bbf_end[] asm("_binary_inter_medium_32_bbf_end");

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

static inline uint16_t read_le_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static inline uint32_t read_le_u32(const uint8_t *p)
{
    return (uint32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

struct NativeRect {
    int32_t x0;
    int32_t y0;
    int32_t w;
    int32_t h;
};

bool compute_native_rect_for_logical_rect(
    int32_t rotation,
    int32_t logical_w,
    int32_t logical_h,
    int32_t dst_x0,
    int32_t dst_y0,
    int32_t draw_w,
    int32_t draw_h,
    int32_t *out_native_w,
    int32_t *out_native_h,
    NativeRect *out)
{
    if (!out_native_w || !out_native_h || !out) {
        wasm_api_set_last_error(kWasmErrInternal, "compute_native_rect_for_logical_rect: null out pointer");
        return false;
    }

    if (logical_w <= 0 || logical_h <= 0 || draw_w < 0 || draw_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "compute_native_rect_for_logical_rect: invalid dimensions");
        return false;
    }

    const bool swap = (rotation == 90) || (rotation == 270);
    const int32_t native_w = swap ? logical_h : logical_w;
    const int32_t native_h = swap ? logical_w : logical_h;

    NativeRect r = {0, 0, 0, 0};
    switch (rotation) {
        case 0:
            r = {dst_x0, dst_y0, draw_w, draw_h};
            break;
        case 90:
            r = {dst_y0, logical_w - (dst_x0 + draw_w), draw_h, draw_w};
            break;
        case 180:
            r = {logical_w - (dst_x0 + draw_w), logical_h - (dst_y0 + draw_h), draw_w, draw_h};
            break;
        case 270:
            r = {logical_h - (dst_y0 + draw_h), dst_x0, draw_h, draw_w};
            break;
        default:
            wasm_api_set_last_error(kWasmErrInvalidArgument, "compute_native_rect_for_logical_rect: unsupported rotation");
            return false;
    }

    if (r.w < 0 || r.h < 0 || r.x0 < 0 || r.y0 < 0 || (r.x0 + r.w) > native_w || (r.y0 + r.h) > native_h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "compute_native_rect_for_logical_rect: rect out of bounds");
        return false;
    }

    *out_native_w = native_w;
    *out_native_h = native_h;
    *out = r;
    return true;
}

static inline uint8_t get_xtg_pixel_1bpp(const uint8_t *buf, uint32_t w, uint32_t x, uint32_t y)
{
    const uint32_t row_bytes = (w + 7u) >> 3;
    const uint32_t byte_index = y * row_bytes + (x >> 3);
    const uint8_t bit = (uint8_t)(7u - (x & 7u));
    return (uint8_t)((buf[byte_index] >> bit) & 0x1u);
}

static inline uint8_t get_xth_code(const uint8_t *plane1, const uint8_t *plane2, uint32_t w, uint32_t h,
    uint32_t x, uint32_t y)
{
    const uint64_t col = ((uint64_t)w - 1u) - (uint64_t)x; // XTH scans columns right-to-left
    const uint64_t p = col * (uint64_t)h + (uint64_t)y;
    const size_t byte_index = (size_t)(p >> 3);
    const uint8_t mask = (uint8_t)(0x80u >> (uint8_t)(p & 7u));
    const uint8_t b1 = (plane1[byte_index] & mask) ? 1u : 0u;
    const uint8_t b2 = (plane2[byte_index] & mask) ? 1u : 0u;
    return (uint8_t)((b1 << 1) | b2);
}

void blit_row_1bpp(uint8_t *fb, int32_t native_w, int32_t y, int32_t x0, int32_t w, const uint8_t *row,
    size_t row_bytes)
{
    if (!fb || !row || native_w <= 0 || y < 0 || x0 < 0 || w <= 0) {
        return;
    }

    const int32_t pitch = (native_w + 7) >> 3;
    const int32_t start_byte = x0 >> 3;
    const uint8_t bit_off = (uint8_t)(x0 & 7);
    const int32_t total_bits = (int32_t)bit_off + w;
    const size_t nbytes = (size_t)((total_bits + 7) >> 3);
    if (nbytes == 0 || row_bytes < nbytes) {
        return;
    }

    uint8_t *dst = fb + (size_t)y * (size_t)pitch + (size_t)start_byte;

    const uint8_t mask_first = (uint8_t)(0xFFu >> bit_off);
    const uint8_t bits_last = (uint8_t)(total_bits & 7);
    const uint8_t mask_last = (bits_last == 0) ? (uint8_t)0xFFu : (uint8_t)(0xFFu << (8u - bits_last));

    if (nbytes == 1) {
        const uint8_t mask = (uint8_t)(mask_first & mask_last);
        dst[0] = (uint8_t)((dst[0] & (uint8_t)~mask) | (row[0] & mask));
        return;
    }

    dst[0] = (uint8_t)((dst[0] & (uint8_t)~mask_first) | (row[0] & mask_first));
    if (nbytes > 2) {
        memcpy(dst + 1, row + 1, nbytes - 2);
    }
    dst[nbytes - 1] = (uint8_t)((dst[nbytes - 1] & (uint8_t)~mask_last) | (row[nbytes - 1] & mask_last));
}

void blit_row_4bpp(uint8_t *fb, int32_t native_w, int32_t y, int32_t x0, int32_t w, const uint8_t *row,
    size_t row_bytes)
{
    if (!fb || !row || native_w <= 0 || y < 0 || x0 < 0 || w <= 0) {
        return;
    }

    const int32_t pitch = native_w >> 1;
    const int32_t start_byte = x0 >> 1;
    const uint8_t nib_off = (uint8_t)(x0 & 1);
    const int32_t total_nibs = (int32_t)nib_off + w;
    const size_t nbytes = (size_t)((total_nibs + 1) >> 1);
    if (nbytes == 0 || row_bytes < nbytes) {
        return;
    }

    uint8_t *dst = fb + (size_t)y * (size_t)pitch + (size_t)start_byte;

    const uint8_t mask_first = (nib_off == 0) ? (uint8_t)0xFFu : (uint8_t)0x0Fu;
    const uint8_t mask_last = ((total_nibs & 1) == 0) ? (uint8_t)0xFFu : (uint8_t)0xF0u;

    if (nbytes == 1) {
        const uint8_t mask = (uint8_t)(mask_first & mask_last);
        dst[0] = (uint8_t)((dst[0] & (uint8_t)~mask) | (row[0] & mask));
        return;
    }

    dst[0] = (uint8_t)((dst[0] & (uint8_t)~mask_first) | (row[0] & mask_first));
    if (nbytes > 2) {
        memcpy(dst + 1, row + 1, nbytes - 2);
    }
    dst[nbytes - 1] = (uint8_t)((dst[nbytes - 1] & (uint8_t)~mask_last) | (row[nbytes - 1] & mask_last));
}

int32_t draw_xth_centered_internal(const uint8_t *xth, size_t xth_size, int32_t mode)
{
    constexpr size_t kHeaderSize = 22;
    constexpr uint32_t kXthMark = 0x00485458u; // "XTH\0" little-endian

    if (!xth || xth_size < kHeaderSize) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: buffer too small for header");
        return kWasmErrInvalidArgument;
    }

    const uint32_t mark = read_le_u32(xth + 0x00);
    if (mark != kXthMark) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: bad magic");
        return kWasmErrInvalidArgument;
    }

    const uint32_t width = (uint32_t)read_le_u16(xth + 0x04);
    const uint32_t height = (uint32_t)read_le_u16(xth + 0x06);
    const uint8_t color_mode = xth[0x08];
    const uint8_t compression = xth[0x09];
    (void)read_le_u32(xth + 0x0A); // data_size (informational)

    if (width == 0 || height == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: invalid dimensions");
        return kWasmErrInvalidArgument;
    }
    if (color_mode != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: unsupported color_mode");
        return kWasmErrInvalidArgument;
    }
    if (compression != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: unsupported compression");
        return kWasmErrInvalidArgument;
    }

    const uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: invalid pixel count");
        return kWasmErrInvalidArgument;
    }

    const uint64_t plane_size64 = (pixel_count + 7u) / 8u;
    const uint64_t data_size64 = plane_size64 * 2u;
    if (plane_size64 > (uint64_t)SIZE_MAX || data_size64 > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: image too large");
        return kWasmErrInvalidArgument;
    }

    const size_t plane_size = (size_t)plane_size64;
    const size_t required_size = kHeaderSize + (size_t)data_size64;
    if (xth_size < required_size) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: truncated data");
        return kWasmErrInvalidArgument;
    }

    const uint8_t *plane1 = xth + kHeaderSize;
    const uint8_t *plane2 = plane1 + plane_size;

    const int32_t logical_w = g_epd.width();
    const int32_t logical_h = g_epd.height();
    if (logical_w <= 0 || logical_h <= 0) {
        wasm_api_set_last_error(kWasmErrNotReady, "draw_xth_centered: display not initialized");
        return kWasmErrNotReady;
    }

    const int32_t decoded_w = (int32_t)width;
    const int32_t decoded_h = (int32_t)height;
    const int32_t draw_w = decoded_w < logical_w ? decoded_w : logical_w;
    const int32_t draw_h = decoded_h < logical_h ? decoded_h : logical_h;
    if (draw_w <= 0 || draw_h <= 0) {
        return kWasmOk;
    }

    const int32_t src_x0 = (decoded_w > draw_w) ? ((decoded_w - draw_w) / 2) : 0;
    const int32_t src_y0 = (decoded_h > draw_h) ? ((decoded_h - draw_h) / 2) : 0;
    const int32_t dst_x0 = (logical_w > draw_w) ? ((logical_w - draw_w) / 2) : 0;
    const int32_t dst_y0 = (logical_h > draw_h) ? ((logical_h - draw_h) / 2) : 0;

    uint8_t *fb = g_epd.currentBuffer();
    if (!fb) {
        wasm_api_set_last_error(kWasmErrNotReady, "draw_xth_centered: framebuffer missing");
        return kWasmErrNotReady;
    }

    const int32_t rotation = (int32_t)g_epd.getRotation();
    int32_t native_w = 0;
    int32_t native_h = 0;
    NativeRect rect = {0, 0, 0, 0};
    if (!compute_native_rect_for_logical_rect(rotation, logical_w, logical_h, dst_x0, dst_y0, draw_w, draw_h,
            &native_w, &native_h, &rect)) {
        return kWasmErrInvalidArgument;
    }
    (void)native_h;

    if (mode == BB_MODE_1BPP) {
        const uint8_t bit_off = (uint8_t)(rect.x0 & 7);
        const size_t row_bytes = (size_t)(((int32_t)bit_off + rect.w + 7) >> 3);
        std::vector<uint8_t> row(row_bytes);

        for (int32_t r = 0; r < rect.h; ++r) {
            memset(row.data(), 0, row_bytes);

            switch (rotation) {
                case 0: {
                    const uint32_t sy = (uint32_t)(src_y0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + c);
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t white = ((xth_code & 1u) == 0u) ? 1u : 0u;
                        if (white) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                case 180: {
                    const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - c));
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t white = ((xth_code & 1u) == 0u) ? 1u : 0u;
                        if (white) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                case 90: {
                    const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + c);
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t white = ((xth_code & 1u) == 0u) ? 1u : 0u;
                        if (white) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                case 270: {
                    const uint32_t sx = (uint32_t)(src_x0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - c));
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t white = ((xth_code & 1u) == 0u) ? 1u : 0u;
                        if (white) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                default:
                    wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: unsupported rotation");
                    return kWasmErrInvalidArgument;
            }

            blit_row_1bpp(fb, native_w, rect.y0 + r, rect.x0, rect.w, row.data(), row_bytes);
        }
    } else if (mode == BB_MODE_4BPP) {
        static constexpr uint8_t kXthCodeTo4bpp[4] = {15, 5, 10, 0};
        const uint8_t nib_off = (uint8_t)(rect.x0 & 1);
        const size_t row_bytes = (size_t)(((int32_t)nib_off + rect.w + 1) >> 1);
        std::vector<uint8_t> row(row_bytes);

        for (int32_t r = 0; r < rect.h; ++r) {
            memset(row.data(), 0, row_bytes);

            switch (rotation) {
                case 0: {
                    const uint32_t sy = (uint32_t)(src_y0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + c);
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t v = kXthCodeTo4bpp[xth_code & 3u];
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                case 180: {
                    const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - c));
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t v = kXthCodeTo4bpp[xth_code & 3u];
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                case 90: {
                    const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + c);
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t v = kXthCodeTo4bpp[xth_code & 3u];
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                case 270: {
                    const uint32_t sx = (uint32_t)(src_x0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - c));
                        const uint8_t xth_code = get_xth_code(plane1, plane2, width, height, sx, sy);
                        const uint8_t v = kXthCodeTo4bpp[xth_code & 3u];
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                default:
                    wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: unsupported rotation");
                    return kWasmErrInvalidArgument;
            }

            blit_row_4bpp(fb, native_w, rect.y0 + r, rect.x0, rect.w, row.data(), row_bytes);
        }
    } else {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth_centered: unsupported mode");
        return kWasmErrInvalidArgument;
    }
    g_epd.fullUpdate(CLEAR_SLOW, false);
    return kWasmOk;
}

int32_t draw_xtg_centered_internal(const uint8_t *xtg, size_t xtg_size, int32_t mode)
{
    constexpr size_t kHeaderSize = 22;
    constexpr uint32_t kXtgMark = 0x00475458u; // "XTG\0" little-endian

    if (!xtg || xtg_size < kHeaderSize) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: buffer too small for header");
        return kWasmErrInvalidArgument;
    }

    const uint32_t mark = read_le_u32(xtg + 0x00);
    if (mark != kXtgMark) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: bad magic");
        return kWasmErrInvalidArgument;
    }

    const uint32_t width = (uint32_t)read_le_u16(xtg + 0x04);
    const uint32_t height = (uint32_t)read_le_u16(xtg + 0x06);
    const uint8_t color_mode = xtg[0x08];
    const uint8_t compression = xtg[0x09];
    (void)read_le_u32(xtg + 0x0A); // data_size (informational)

    if (width == 0 || height == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: invalid dimensions");
        return kWasmErrInvalidArgument;
    }
    if (color_mode != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: unsupported color_mode");
        return kWasmErrInvalidArgument;
    }
    if (compression != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: unsupported compression");
        return kWasmErrInvalidArgument;
    }

    const uint64_t row_bytes64 = ((uint64_t)width + 7u) / 8u;
    const uint64_t data_size64 = row_bytes64 * (uint64_t)height;
    if (row_bytes64 > (uint64_t)SIZE_MAX || data_size64 > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: image too large");
        return kWasmErrInvalidArgument;
    }

    const size_t expected_data_size = (size_t)data_size64;
    const size_t required_size = kHeaderSize + expected_data_size;
    if (xtg_size < required_size) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: truncated data");
        return kWasmErrInvalidArgument;
    }

    const uint8_t *image_data = xtg + kHeaderSize;

    const int32_t logical_w = g_epd.width();
    const int32_t logical_h = g_epd.height();
    if (logical_w <= 0 || logical_h <= 0) {
        wasm_api_set_last_error(kWasmErrNotReady, "draw_xtg_centered: display not initialized");
        return kWasmErrNotReady;
    }

    const int32_t decoded_w = (int32_t)width;
    const int32_t decoded_h = (int32_t)height;
    const int32_t draw_w = decoded_w < logical_w ? decoded_w : logical_w;
    const int32_t draw_h = decoded_h < logical_h ? decoded_h : logical_h;
    if (draw_w <= 0 || draw_h <= 0) {
        return kWasmOk;
    }

    const int32_t src_x0 = (decoded_w > draw_w) ? ((decoded_w - draw_w) / 2) : 0;
    const int32_t src_y0 = (decoded_h > draw_h) ? ((decoded_h - draw_h) / 2) : 0;
    const int32_t dst_x0 = (logical_w > draw_w) ? ((logical_w - draw_w) / 2) : 0;
    const int32_t dst_y0 = (logical_h > draw_h) ? ((logical_h - draw_h) / 2) : 0;

    uint8_t *fb = g_epd.currentBuffer();
    if (!fb) {
        wasm_api_set_last_error(kWasmErrNotReady, "draw_xtg_centered: framebuffer missing");
        return kWasmErrNotReady;
    }

    const int32_t rotation = (int32_t)g_epd.getRotation();
    int32_t native_w = 0;
    int32_t native_h = 0;
    NativeRect rect = {0, 0, 0, 0};
    if (!compute_native_rect_for_logical_rect(rotation, logical_w, logical_h, dst_x0, dst_y0, draw_w, draw_h,
            &native_w, &native_h, &rect)) {
        return kWasmErrInvalidArgument;
    }
    (void)native_h;

    if (mode == BB_MODE_1BPP) {
        const uint8_t bit_off = (uint8_t)(rect.x0 & 7);
        const size_t row_bytes = (size_t)(((int32_t)bit_off + rect.w + 7) >> 3);
        std::vector<uint8_t> row(row_bytes);

        for (int32_t r = 0; r < rect.h; ++r) {
            memset(row.data(), 0, row_bytes);

            switch (rotation) {
                case 0: {
                    const uint32_t sy = (uint32_t)(src_y0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + c);
                        const uint8_t v = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        if (v) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                case 180: {
                    const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - c));
                        const uint8_t v = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        if (v) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                case 90: {
                    const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + c);
                        const uint8_t v = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        if (v) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                case 270: {
                    const uint32_t sx = (uint32_t)(src_x0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - c));
                        const uint8_t v = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        if (v) {
                            const uint32_t bitpos = (uint32_t)bit_off + (uint32_t)c;
                            row[bitpos >> 3] |= (uint8_t)(0x80u >> (bitpos & 7u));
                        }
                    }
                } break;
                default:
                    wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: unsupported rotation");
                    return kWasmErrInvalidArgument;
            }

            blit_row_1bpp(fb, native_w, rect.y0 + r, rect.x0, rect.w, row.data(), row_bytes);
        }
    } else if (mode == BB_MODE_4BPP) {
        const uint8_t nib_off = (uint8_t)(rect.x0 & 1);
        const size_t row_bytes = (size_t)(((int32_t)nib_off + rect.w + 1) >> 1);
        std::vector<uint8_t> row(row_bytes);

        for (int32_t r = 0; r < rect.h; ++r) {
            memset(row.data(), 0, row_bytes);

            switch (rotation) {
                case 0: {
                    const uint32_t sy = (uint32_t)(src_y0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + c);
                        const uint8_t on = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        const uint8_t v = on ? (uint8_t)0xFu : (uint8_t)0u;
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                case 180: {
                    const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - c));
                        const uint8_t on = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        const uint8_t v = on ? (uint8_t)0xFu : (uint8_t)0u;
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                case 90: {
                    const uint32_t sx = (uint32_t)(src_x0 + (draw_w - 1 - r));
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + c);
                        const uint8_t on = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        const uint8_t v = on ? (uint8_t)0xFu : (uint8_t)0u;
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                case 270: {
                    const uint32_t sx = (uint32_t)(src_x0 + r);
                    for (int32_t c = 0; c < rect.w; ++c) {
                        const uint32_t sy = (uint32_t)(src_y0 + (draw_h - 1 - c));
                        const uint8_t on = get_xtg_pixel_1bpp(image_data, width, sx, sy);
                        const uint8_t v = on ? (uint8_t)0xFu : (uint8_t)0u;
                        const uint32_t npos = (uint32_t)nib_off + (uint32_t)c;
                        const size_t bi = (size_t)(npos >> 1);
                        if ((npos & 1u) == 0u) {
                            row[bi] = (uint8_t)((row[bi] & 0x0Fu) | (uint8_t)(v << 4));
                        } else {
                            row[bi] = (uint8_t)((row[bi] & 0xF0u) | v);
                        }
                    }
                } break;
                default:
                    wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: unsupported rotation");
                    return kWasmErrInvalidArgument;
            }

            blit_row_4bpp(fb, native_w, rect.y0 + r, rect.x0, rect.w, row.data(), row_bytes);
        }
    } else {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg_centered: unsupported mode");
        return kWasmErrInvalidArgument;
    }
    g_epd.fullUpdate(CLEAR_SLOW, false);
    return kWasmOk;
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

	extern "C" void show_sleepimage_with_fastepd_best_effort(void)
	{
    constexpr int kPortraitRotationDeg = 90;

    const uint8_t *start = _binary_sleepimage_jpg_start;
    const uint8_t *end = _binary_sleepimage_jpg_end;
    if (end <= start) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] sleepimage asset missing/empty");
        return;
    }

    const bool was_inited = g_epd_inited;
    auto cleanup_if_owned = [&]() {
        if (was_inited) {
            return;
        }
        g_epd.deInit();
        bbepDeinitBus();
        g_epd_inited = false;
    };

    if (!ensure_epd_ready()) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] FastEPD init failed");
        cleanup_if_owned();
        return;
    }

    (void)g_epd.setMode(BB_MODE_4BPP);
    (void)g_epd.setRotation(kPortraitRotationDeg);
    g_epd.fillScreen(0xF);

    if (!g_epd.currentBuffer()) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] FastEPD framebuffer missing");
        cleanup_if_owned();
        return;
    }

    const size_t len = (size_t)(end - start);
    JpegDrawContext ctx = {};
    ctx.epd = &g_epd;
    ctx.mode = g_epd.getMode();
    ctx.clip_x0 = 0;
	    ctx.clip_y0 = 0;
	    ctx.clip_x1 = g_epd.width();
	    ctx.clip_y1 = g_epd.height();

	    JPEGIMAGE *jpeg = (JPEGIMAGE *)calloc(1, sizeof(JPEGIMAGE));
	    if (!jpeg) {
	        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] JPEGIMAGE alloc failed");
	        cleanup_if_owned();
	        return;
	    }

	    const int opened = JPEG_openRAM(jpeg, (uint8_t *)start, (int)len, epd_jpeg_draw);
	    if (!opened) {
	        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] JPEG openRAM failed (%d)",
	                 (int)JPEG_getLastError(jpeg));
	        free(jpeg);
	        cleanup_if_owned();
	        return;
	    }
	    jpeg->pUser = &ctx;

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

	    const int img_w = JPEG_getWidth(jpeg);
	    const int cx = base_mcu_w == 16 ? ((img_w + 15) >> 4) : ((img_w + 7) >> 3);
	    const size_t aligned_w = (size_t)cx * (size_t)base_mcu_w;
	    const size_t dither_buf_len = aligned_w * (size_t)base_mcu_h;

	    bool ok = false;
	    uint8_t *dither_buf = nullptr;
	    if (dither_buf_len > 0) {
	        dither_buf = (uint8_t *)malloc(dither_buf_len);
	    }

	    if (dither_buf) {
	        JPEG_setPixelType(jpeg, FOUR_BIT_DITHERED);
	        jpeg->iXOffset = 0;
	        jpeg->iYOffset = 0;
	        ok = JPEG_decodeDither(jpeg, dither_buf, 0) != 0;
	        free(dither_buf);
	    } else {
	        if (dither_buf_len > 0) {
	            ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] dither buffer alloc failed (%u bytes)",
	                     (unsigned)dither_buf_len);
	        }
	        JPEG_setPixelType(jpeg, EIGHT_BIT_GRAYSCALE);
	        ok = JPEG_decode(jpeg, 0, 0, 0) != 0;
	    }

	    const int last_err = JPEG_getLastError(jpeg);
	    JPEG_close(jpeg);
	    free(jpeg);

	    if (!ok) {
	        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] JPEG decode failed (%d)", last_err);
	        cleanup_if_owned();
        return;
    }

    const int epd_rc = g_epd.fullUpdate(CLEAR_SLOW, false);
    if (epd_rc != BBEP_SUCCESS) {
        ESP_LOGW(kTag, "[show_sleepimage_with_fastepd_best_effort] FastEPD fullUpdate failed (%d)", epd_rc);
        cleanup_if_owned();
        return;
    }

    cleanup_if_owned();
}

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
    const int epd_rc = g_epd.fullUpdate(CLEAR_FAST, false);
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
    (void)exec_env;
    const int32_t rc = require_epd_ready_or_set_error("setEpdMode: display not ready");
    if (rc != kWasmOk) {
        return rc;
    }
    if (mode < 1 || mode > 4) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setEpdMode: mode out of range (1..4)");
        return kWasmErrInvalidArgument;
    }
    const int32_t new_mode = (mode == 1) ? BB_MODE_1BPP : BB_MODE_4BPP;
    const int epd_rc = g_epd.setMode(new_mode);
    if (epd_rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "setEpdMode: FastEPD setMode failed");
        return kWasmErrInternal;
    }
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
    //g_epd.setCursor(x, y);
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

int32_t DisplayFastEpd::vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id)
{
    const void *font_ptr = nullptr;
    switch (font_id) {
    case kVlwSystemFontInter:
        font_ptr = _binary_inter_medium_32_bbf_start;
        break;
    default:
        ESP_LOGI(kTag, "vlwUseSystem rejected invalid font_id=%" PRId32, font_id);
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwUseSystem: invalid font_id");
        return kWasmErrInvalidArgument;
    }
    g_epd.setFont(FONT_16x16);
    //g_epd.setFont(font_ptr, false);
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

int32_t DisplayFastEpd::drawXthCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
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

    return draw_xth_centered_internal(ptr, len, mode);
}

int32_t DisplayFastEpd::drawXtgCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
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

    return draw_xtg_centered_internal(ptr, len, mode);
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
    (void)x;
    (void)y;
    (void)r0;
    (void)r1;
    (void)angle0;
    (void)angle1;
    (void)rgb888;
    warn_unimplemented("fillArc");
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
