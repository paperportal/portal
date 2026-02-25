/**
 * @file fastepd_draw_xtc.cpp
 * @brief Implementation of XTG/XTH drawing helpers for FastEPD.
 */

#include "fastepd_xtc.h"
#include "fastepd_xtc_utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <FastEPD.h>

/// @brief ESP_LOG tag for this translation unit.
static const char* TAG = "fastepd_draw_xtc";

void draw_xth(FASTEPD* epd, const uint8_t* data, size_t size, bool fast) {
    if (!epd || !data) {
        ESP_LOGE(TAG, "draw_xth: null epd=%p data=%p", epd, data);
        return;
    }

    const int32_t mode = epd->getMode();
    const int64_t start_us = esp_timer_get_time();

    XtxImageHeader hdr = {};
    const uint8_t* payload = nullptr;
    if (!parse_xth_header(data, size, &hdr, &payload)) {
        ESP_LOGE(TAG, "draw_xth: invalid XTH header (size=%zu)", size);
        return;
    }
    if (hdr.color_mode != 0 || hdr.compression != 0) {
        ESP_LOGE(TAG, "draw_xth: unsupported XTH header: colorMode=%u compression=%u", hdr.color_mode, hdr.compression);
        return;
    }

    if (mode != BB_MODE_2BPP) {
        ESP_LOGE(TAG, "draw_xth: requires BB_MODE_2BPP (mode=%d)", static_cast<int>(mode));
        return;
    }

    int rot = epd->getRotation();
    rot %= 360;
    if (rot < 0) {
        rot += 360;
    }
    if (rot != 0 && rot != 90 && rot != 180 && rot != 270) {
        ESP_LOGE(TAG, "draw_xth: unsupported rotation=%d (expected 0/90/180/270)", rot);
        return;
    }

    const int logical_w = epd->width();
    const int logical_h = epd->height();
    if (hdr.width == 0 || hdr.height == 0 || logical_w <= 0 || logical_h <= 0) {
        ESP_LOGE(TAG, "draw_xth: invalid dimensions: xth=%ux%u epd=%dx%d", hdr.width, hdr.height, logical_w, logical_h);
        return;
    }

    const int src_w = static_cast<int>(hdr.width);
    const int src_h = static_cast<int>(hdr.height);
    const size_t plane_bytes = static_cast<size_t>(src_w) * static_cast<size_t>((src_h + 7) >> 3);
    const size_t expected_bytes = plane_bytes * 2u;
    if (hdr.data_size != expected_bytes) {
        ESP_LOGE(TAG, "draw_xth: dataSize mismatch: hdr=%" PRIu32 " expected=%zu", hdr.data_size, expected_bytes);
        return;
    }
    if (kXtxHeaderSize + expected_bytes > size) {
        ESP_LOGE(TAG, "draw_xth: truncated payload: size=%zu need=%zu", size, kXtxHeaderSize + expected_bytes);
        return;
    }

    const uint8_t* plane1 = payload;
    const uint8_t* plane2 = payload + plane_bytes;

    uint8_t* fb = epd->currentBuffer();
    if (!fb) {
        ESP_LOGE(TAG, "draw_xth: epd.currentBuffer() returned null");
        return;
    }

    const int copy_w = (src_w < logical_w) ? src_w : logical_w;
    const int copy_h = (src_h < logical_h) ? src_h : logical_h;
    const bool covers_fullscreen = (copy_w == logical_w) && (copy_h == logical_h);

    if (rot == 0) {
        const int native_w = logical_w;
        const int native_h = logical_h;
        if ((native_w & 3) != 0) {
            ESP_LOGE(TAG, "draw_xth: rotation=0 requires width multiple of 4 (w=%d)", native_w);
            return;
        }
        const int dst_pitch = native_w >> 2;
        if (!covers_fullscreen) {
            clear_native_white_2bpp(fb, dst_pitch, native_h);
        }
        xth_blit_rot0_topleft_clipped_2bpp(fb, dst_pitch, plane1, plane2, src_w, src_h, copy_w, copy_h);
    } else if (rot == 90) {
        // Rotation=90 => native_w==logical_h, native_h==logical_w
        const int native_w = logical_h;
        const int native_h = logical_w;
        if ((native_w & 3) != 0) {
            ESP_LOGE(TAG, "draw_xth: rotation=90 requires native width multiple of 4 (native_w=%d)", native_w);
            return;
        }
        const int dst_pitch = native_w >> 2;
        if (!covers_fullscreen) {
            clear_native_white_2bpp(fb, dst_pitch, native_h);
        }
        xth_blit_rot90_topleft_clipped_2bpp(fb, dst_pitch, logical_w, plane1, plane2, src_w, src_h, copy_w, copy_h);
    } else if (rot == 180) {
        const int native_w = logical_w;
        const int native_h = logical_h;
        if ((native_w & 3) != 0) {
            ESP_LOGE(TAG, "draw_xth: rotation=180 requires width multiple of 4 (w=%d)", native_w);
            return;
        }
        const int dst_pitch = native_w >> 2;
        if (!covers_fullscreen) {
            clear_native_white_2bpp(fb, dst_pitch, native_h);
        }
        xth_blit_rot180_topleft_clipped_2bpp(
            fb, dst_pitch, logical_w, logical_h, plane1, plane2, src_w, src_h, copy_w, copy_h);
    } else { // 270
        // Rotation=270 => native_w==logical_h, native_h==logical_w
        const int native_w = logical_h;
        const int native_h = logical_w;
        if ((native_w & 3) != 0) {
            ESP_LOGE(TAG, "draw_xth: rotation=270 requires native width multiple of 4 (native_w=%d)", native_w);
            return;
        }
        const int dst_pitch = native_w >> 2;
        if (!covers_fullscreen) {
            clear_native_white_2bpp(fb, dst_pitch, native_h);
        }
        xth_blit_rot270_topleft_clipped_2bpp(
            fb, dst_pitch, logical_h, plane1, plane2, src_w, src_h, copy_w, copy_h);
    }

    const int64_t draw_done_us = esp_timer_get_time();
    if (fast) {
        epd->smoothUpdate(true, BBEP_WHITE);
    } else {
        epd->fullUpdate(CLEAR_WHITE, true);
    }
    const int64_t end_us = esp_timer_get_time();

    const int64_t draw_us = draw_done_us - start_us;
    const int64_t update_us = end_us - draw_done_us;
    const int64_t total_us = end_us - start_us;
    ESP_LOGI(TAG,
        "draw_xth: draw=%lld us update=%lld us total=%lld us rot=%d mode=%d",
        static_cast<long long>(draw_us),
        static_cast<long long>(update_us),
        static_cast<long long>(total_us),
        rot,
        static_cast<int>(mode));
}

void draw_xtg(FASTEPD* epd, const uint8_t* data, size_t size, bool fast) {
    if (!epd || !data) {
        ESP_LOGE(TAG, "draw_xtg: null epd=%p data=%p", epd, data);
        return;
    }

    int32_t mode = epd->getMode();
    const int64_t start_us = esp_timer_get_time();

    XtxImageHeader hdr = {};
    const uint8_t* payload = nullptr;
    if (!parse_xtg_header(data, size, &hdr, &payload)) {
        ESP_LOGE(TAG, "draw_xtg: invalid XTG header (size=%zu)", size);
        return;
    }
    if (hdr.color_mode != 0 || hdr.compression != 0) {
        ESP_LOGE(TAG, "draw_xtg: unsupported XTG header: colorMode=%u compression=%u", hdr.color_mode, hdr.compression);
        return;
    }

    const int logical_w = epd->width();
    const int logical_h = epd->height();
    if (hdr.width == 0 || hdr.height == 0 || logical_w <= 0 || logical_h <= 0) {
        ESP_LOGE(TAG, "draw_xtg: invalid dimensions: xtg=%ux%u epd=%dx%d", hdr.width, hdr.height, logical_w, logical_h);
        return;
    }

    if (mode != BB_MODE_1BPP) {
        ESP_LOGE(TAG, "draw_xtg: requires BB_MODE_1BPP (mode=%d)", static_cast<int>(mode));
        return;
    }

    const int src_w = static_cast<int>(hdr.width);
    const int src_h = static_cast<int>(hdr.height);
    const int src_pitch = (src_w + 7) >> 3;
    const size_t expected_bytes = static_cast<size_t>(src_pitch) * static_cast<size_t>(src_h);
    if (hdr.data_size != expected_bytes) {
        ESP_LOGE(TAG, "draw_xtg: dataSize mismatch: hdr=%" PRIu32 " expected=%zu", hdr.data_size, expected_bytes);
        return;
    }
    if (kXtxHeaderSize + expected_bytes > size) {
        ESP_LOGE(TAG, "draw_xtg: truncated payload: size=%zu need=%zu", size, kXtxHeaderSize + expected_bytes);
        return;
    }

    uint8_t* fb = epd->currentBuffer();
    if (!fb) {
        ESP_LOGE(TAG, "draw_xtg: epd.currentBuffer() returned null");
        return;
    }

    int rot = epd->getRotation();
    rot %= 360;
    if (rot < 0) {
        rot += 360;
    }

    const int copy_w = (src_w < logical_w) ? src_w : logical_w;
    const int copy_h = (src_h < logical_h) ? src_h : logical_h;
    const bool covers_fullscreen = (copy_w == logical_w) && (copy_h == logical_h);
    const bool exact_match = (src_w == logical_w) && (src_h == logical_h);

    if (rot == 0) {
        const int dst_pitch = (logical_w + 7) >> 3;
        if (!covers_fullscreen) {
            clear_native_white_1bpp(fb, dst_pitch, logical_h);
        }
        if (exact_match) {
            xtg_blit_rot0_fullscreen_1bpp(fb, payload, logical_w, logical_h);
        } else {
            xtg_blit_rot0_topleft_clipped_1bpp(fb, dst_pitch, payload, src_pitch, copy_w, copy_h);
            // Ensure padding bits in the destination width are white.
            const int r = logical_w & 7;
            if (r != 0) {
                const uint8_t pad_mask = static_cast<uint8_t>((1u << (8 - r)) - 1u);
                uint8_t* row = fb + (dst_pitch - 1);
                for (int y = 0; y < logical_h; y++) {
                    row[static_cast<size_t>(y) * static_cast<size_t>(dst_pitch)] |= pad_mask;
                }
            }
        }
    } else if (rot == 90) {
        const int dst_pitch = (logical_h + 7) >> 3; // native_w == logical_h for rot=90
        if (!covers_fullscreen) {
            clear_native_white_1bpp(fb, dst_pitch, logical_w); // native_h == logical_w for rot=90
        }
        xtg_blit_rot90_topleft_clipped_1bpp(fb, dst_pitch, logical_w, payload, src_pitch, src_w, copy_w, copy_h);
    } else if (rot == 180) {
        const int dst_pitch = (logical_w + 7) >> 3;
        if (!covers_fullscreen) {
            clear_native_white_1bpp(fb, dst_pitch, logical_h);
        }
        xtg_blit_rot180_topleft_clipped_1bpp(
            fb, dst_pitch, logical_w, logical_h, payload, src_pitch, src_w, copy_w, copy_h);
    } else if (rot == 270) {
        const int dst_pitch = (logical_h + 7) >> 3; // native_w == logical_h for rot=270
        if (!covers_fullscreen) {
            clear_native_white_1bpp(fb, dst_pitch, logical_w); // native_h == logical_w for rot=270
        }
        xtg_blit_rot270_topleft_clipped_1bpp(fb, dst_pitch, logical_h, payload, src_pitch, src_w, copy_w, copy_h);
    } else {
        ESP_LOGE(TAG, "draw_xtg: unsupported rotation=%d (expected 0/90/180/270)", rot);
        return;
    }

    const int64_t draw_done_us = esp_timer_get_time();
//    epd->fullUpdate(fast ? CLEAR_FAST : CLEAR_SLOW, true);
    if (fast) {
        epd->smoothUpdate(true, BBEP_WHITE);
        } else {
        epd->fullUpdate(CLEAR_WHITE, true);
     }
    const int64_t end_us = esp_timer_get_time();

    const int64_t draw_us = draw_done_us - start_us;
    const int64_t update_us = end_us - draw_done_us;
    const int64_t total_us = end_us - start_us;
    ESP_LOGI(TAG,
        "draw_xtg: draw=%lld us update=%lld us total=%lld us rot=%d mode=%d",
        static_cast<long long>(draw_us),
        static_cast<long long>(update_us),
        static_cast<long long>(total_us),
        rot,
        static_cast<int>(mode));
}
