#include "lovyangfx_xtc.h"

#include <inttypes.h>

#include <algorithm>
#include <limits>

#include "esp_log.h"
#include "m5papers3_display.h"

namespace lovyangfx_xtc {

static const char *kXthTag = "xth";
static const char *kXtgTag = "xtg";

static constexpr lgfx::grayscale_t kGray2Palette[4] = {
    lgfx::grayscale_t(0),
    lgfx::grayscale_t(85),
    lgfx::grayscale_t(170),
    lgfx::grayscale_t(255),
};

static constexpr lgfx::grayscale_t kGray1Palette[2] = {
    lgfx::grayscale_t(0),
    lgfx::grayscale_t(255),
};

static inline uint16_t read_le_u16(const uint8_t *p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

static inline uint32_t read_le_u32(const uint8_t *p) {
    return static_cast<uint32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                 (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

static inline void set_pixel_2bpp(uint8_t *buf, int32_t w, int32_t x, int32_t y, uint8_t v) {
    const uint32_t idx = static_cast<uint32_t>(y) * static_cast<uint32_t>(w) + static_cast<uint32_t>(x);
    const uint32_t byte_index = idx >> 2;
    const uint32_t shift = (3u - (idx & 3u)) * 2u;
    buf[byte_index] = (buf[byte_index] & static_cast<uint8_t>(~(0x3u << shift))) |
                      static_cast<uint8_t>((v & 0x3u) << shift);
}

static inline uint8_t get_pixel_2bpp(const uint8_t *buf, int32_t w, int32_t x, int32_t y) {
    const uint32_t idx = static_cast<uint32_t>(y) * static_cast<uint32_t>(w) + static_cast<uint32_t>(x);
    const uint32_t byte_index = idx >> 2;
    const uint32_t shift = (3u - (idx & 3u)) * 2u;
    return static_cast<uint8_t>((buf[byte_index] >> shift) & 0x3u);
}

static inline void set_pixel_1bpp(uint8_t *buf, int32_t w, int32_t x, int32_t y, uint8_t v) {
    const uint32_t row_bytes = (static_cast<uint32_t>(w) + 7u) >> 3;
    const uint32_t byte_index = static_cast<uint32_t>(y) * row_bytes + (static_cast<uint32_t>(x) >> 3);
    const uint8_t bit = static_cast<uint8_t>(7u - (static_cast<uint32_t>(x) & 7u));
    const uint8_t mask = static_cast<uint8_t>(1u << bit);
    buf[byte_index] = static_cast<uint8_t>((buf[byte_index] & static_cast<uint8_t>(~mask)) | ((v & 0x1u) << bit));
}

static inline uint8_t get_pixel_1bpp(const uint8_t *buf, int32_t w, int32_t x, int32_t y) {
    const uint32_t row_bytes = (static_cast<uint32_t>(w) + 7u) >> 3;
    const uint32_t byte_index = static_cast<uint32_t>(y) * row_bytes + (static_cast<uint32_t>(x) >> 3);
    const uint8_t bit = static_cast<uint8_t>(7u - (static_cast<uint32_t>(x) & 7u));
    return static_cast<uint8_t>((buf[byte_index] >> bit) & 0x1u);
}

bool convertXth(const uint8_t *xth, size_t xth_size, std::vector<uint8_t> &out2bpp, int32_t &out_w, int32_t &out_h) {
    constexpr size_t kHeaderSize = 22;
    constexpr uint32_t kXthMark = 0x00485458u; // "XTH\0" little-endian

    out2bpp.clear();
    out_w = 0;
    out_h = 0;

    if (xth == nullptr || xth_size < kHeaderSize) {
        ESP_LOGE(kXthTag, "XTH: buffer too small for header (%zu)", xth_size);
        return false;
    }

    const uint32_t mark = read_le_u32(xth + 0x00);
    if (mark != kXthMark) {
        ESP_LOGE(kXthTag, "XTH: bad mark 0x%08" PRIx32, mark);
        return false;
    }

    const uint16_t width_u16 = read_le_u16(xth + 0x04);
    const uint16_t height_u16 = read_le_u16(xth + 0x06);
    const uint8_t color_mode = xth[0x08];
    const uint8_t compression = xth[0x09];
    const uint32_t header_data_size = read_le_u32(xth + 0x0A);

    if (width_u16 == 0 || height_u16 == 0) {
        ESP_LOGE(kXthTag, "XTH: invalid dimensions %ux%u", width_u16, height_u16);
        return false;
    }
    if (color_mode != 0) {
        ESP_LOGE(kXthTag, "XTH: unsupported colorMode=%u", color_mode);
        return false;
    }
    if (compression != 0) {
        ESP_LOGE(kXthTag, "XTH: unsupported compression=%u", compression);
        return false;
    }

    const uint32_t width = width_u16;
    const uint32_t height = height_u16;
    const uint64_t pixel_count = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (pixel_count == 0) {
        ESP_LOGE(kXthTag, "XTH: pixel_count overflow/invalid");
        return false;
    }

    const uint64_t plane_size64 = (pixel_count + 7u) / 8u;
    const uint64_t data_size64 = plane_size64 * 2u;
    const uint64_t size_max = static_cast<uint64_t>(std::numeric_limits<size_t>::max());
    if (plane_size64 > size_max || data_size64 > size_max) {
        ESP_LOGE(kXthTag, "XTH: image too large");
        return false;
    }

    const size_t plane_size = static_cast<size_t>(plane_size64);
    const size_t expected_data_size = static_cast<size_t>(data_size64);

    if (header_data_size != expected_data_size) {
        ESP_LOGW(kXthTag, "XTH: header dataSize=%" PRIu32 " computed=%zu", header_data_size, expected_data_size);
    }

    const size_t required_size = kHeaderSize + expected_data_size;
    if (xth_size < required_size) {
        ESP_LOGE(kXthTag, "XTH: truncated data (need %zu bytes, have %zu)", required_size, xth_size);
        return false;
    }

    const uint8_t *plane1 = xth + kHeaderSize;
    const uint8_t *plane2 = plane1 + plane_size;

    const uint64_t out_size64 = (pixel_count + 3u) / 4u;
    if (out_size64 > size_max) {
        ESP_LOGE(kXthTag, "XTH: output too large");
        return false;
    }

    out2bpp.assign(static_cast<size_t>(out_size64), 0xFF);
    out_w = static_cast<int32_t>(width);
    out_h = static_cast<int32_t>(height);

    // Map XTH pixel values (0=white..3=black) to our palette indices (0=black..3=white).
    static constexpr uint8_t kXthToLocal2bpp[4] = {3, 1, 2, 0};

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t col = (width - 1u) - x; // XTH scans columns right-to-left
            const uint64_t p = static_cast<uint64_t>(col) * static_cast<uint64_t>(height) + static_cast<uint64_t>(y);
            const size_t byte_index = static_cast<size_t>(p >> 3);
            const uint8_t mask = static_cast<uint8_t>(0x80u >> static_cast<uint8_t>(p & 7u));

            const uint8_t bit1 = (plane1[byte_index] & mask) ? 1u : 0u;
            const uint8_t bit2 = (plane2[byte_index] & mask) ? 1u : 0u;
            const uint8_t pixel_value = static_cast<uint8_t>((bit1 << 1) | bit2);
            const uint8_t mapped = kXthToLocal2bpp[pixel_value & 3u];

            set_pixel_2bpp(out2bpp.data(), static_cast<int32_t>(width), static_cast<int32_t>(x),
                           static_cast<int32_t>(y), mapped);
        }
    }

    ESP_LOGI(kXthTag, "XTH: decoded %ux%u to %zu bytes (2bpp)", width_u16, height_u16, out2bpp.size());
    return true;
}

bool drawXth(LGFX_M5PaperS3 &display, const uint8_t *xth, size_t xth_size) {
    std::vector<uint8_t> decoded_buf;
    int32_t decoded_w = 0;
    int32_t decoded_h = 0;
    if (!convertXth(xth, xth_size, decoded_buf, decoded_w, decoded_h)) {
        ESP_LOGE(kXthTag, "XTH: convertXth failed; aborting draw");
        return false;
    }

    const int32_t disp_w = display.width();
    const int32_t disp_h = display.height();
    if (disp_w <= 0 || disp_h <= 0) {
        ESP_LOGE(kXthTag, "XTH: invalid display size %" PRId32 "x%" PRId32, disp_w, disp_h);
        return false;
    }
    if (decoded_w <= 0 || decoded_h <= 0) {
        ESP_LOGE(kXthTag, "XTH: invalid decoded size %" PRId32 "x%" PRId32, decoded_w, decoded_h);
        return false;
    }

    const int32_t draw_w = std::min(decoded_w, disp_w);
    const int32_t draw_h = std::min(decoded_h, disp_h);
    const int32_t src_x0 = (decoded_w > draw_w) ? ((decoded_w - draw_w) / 2) : 0;
    const int32_t src_y0 = (decoded_h > draw_h) ? ((decoded_h - draw_h) / 2) : 0;
    const int32_t dst_x0 = (disp_w > draw_w) ? ((disp_w - draw_w) / 2) : 0;
    const int32_t dst_y0 = (disp_h > draw_h) ? ((disp_h - draw_h) / 2) : 0;

    ESP_LOGI(kXthTag,
             "XTH: display %" PRId32 "x%" PRId32 ", decoded %" PRId32 "x%" PRId32 ", drawing %" PRId32 "x%" PRId32
             " at (%" PRId32 ",%" PRId32 ") from (%" PRId32 ",%" PRId32 ")",
             disp_w, disp_h, decoded_w, decoded_h, draw_w, draw_h, dst_x0, dst_y0, src_x0, src_y0);

    if (draw_w == decoded_w && draw_h == decoded_h) {
        display.pushImage(dst_x0, dst_y0, decoded_w, decoded_h, decoded_buf.data(), lgfx::color_depth_t::grayscale_2bit,
                          kGray2Palette);
        return true;
    }

    const uint64_t crop_pixels = static_cast<uint64_t>(draw_w) * static_cast<uint64_t>(draw_h);
    const size_t crop_size = static_cast<size_t>((crop_pixels + 3u) / 4u);
    std::vector<uint8_t> crop_buf(crop_size, 0xFF);
    for (int32_t yy = 0; yy < draw_h; ++yy) {
        for (int32_t xx = 0; xx < draw_w; ++xx) {
            const uint8_t v = get_pixel_2bpp(decoded_buf.data(), decoded_w, src_x0 + xx, src_y0 + yy);
            set_pixel_2bpp(crop_buf.data(), draw_w, xx, yy, v);
        }
    }

    display.pushImage(dst_x0, dst_y0, draw_w, draw_h, crop_buf.data(), lgfx::color_depth_t::grayscale_2bit,
                      kGray2Palette);
    return true;
}

bool drawXtg(LGFX_M5PaperS3 &display, const uint8_t *xtg, size_t xtg_size) {
    constexpr size_t kHeaderSize = 22;
    constexpr uint32_t kXtgMark = 0x00475458u; // "XTG\0" little-endian

    if (xtg == nullptr || xtg_size < kHeaderSize) {
        ESP_LOGE(kXtgTag, "XTG: buffer too small for header (%zu)", xtg_size);
        return false;
    }

    const uint32_t mark = read_le_u32(xtg + 0x00);
    if (mark != kXtgMark) {
        ESP_LOGE(kXtgTag, "XTG: bad mark 0x%08" PRIx32, mark);
        return false;
    }

    const uint16_t width_u16 = read_le_u16(xtg + 0x04);
    const uint16_t height_u16 = read_le_u16(xtg + 0x06);
    const uint8_t color_mode = xtg[0x08];
    const uint8_t compression = xtg[0x09];
    const uint32_t header_data_size = read_le_u32(xtg + 0x0A);

    if (width_u16 == 0 || height_u16 == 0) {
        ESP_LOGE(kXtgTag, "XTG: invalid dimensions %ux%u", width_u16, height_u16);
        return false;
    }
    if (color_mode != 0) {
        ESP_LOGE(kXtgTag, "XTG: unsupported colorMode=%u", color_mode);
        return false;
    }
    if (compression != 0) {
        ESP_LOGE(kXtgTag, "XTG: unsupported compression=%u", compression);
        return false;
    }

    const uint32_t width = width_u16;
    const uint32_t height = height_u16;
    const uint64_t row_bytes64 = (static_cast<uint64_t>(width) + 7u) / 8u;
    const uint64_t data_size64 = row_bytes64 * static_cast<uint64_t>(height);
    const uint64_t size_max = static_cast<uint64_t>(std::numeric_limits<size_t>::max());
    if (data_size64 > size_max) {
        ESP_LOGE(kXtgTag, "XTG: image too large");
        return false;
    }

    const size_t expected_data_size = static_cast<size_t>(data_size64);
    if (header_data_size != expected_data_size) {
        ESP_LOGW(kXtgTag, "XTG: header dataSize=%" PRIu32 " computed=%zu", header_data_size, expected_data_size);
    }

    const size_t required_size = kHeaderSize + expected_data_size;
    if (xtg_size < required_size) {
        ESP_LOGE(kXtgTag, "XTG: truncated data (need %zu bytes, have %zu)", required_size, xtg_size);
        return false;
    }

    const uint8_t *image_data = xtg + kHeaderSize;
    const int32_t decoded_w = static_cast<int32_t>(width);
    const int32_t decoded_h = static_cast<int32_t>(height);

    const int32_t disp_w = display.width();
    const int32_t disp_h = display.height();
    if (disp_w <= 0 || disp_h <= 0) {
        ESP_LOGE(kXtgTag, "XTG: invalid display size %" PRId32 "x%" PRId32, disp_w, disp_h);
        return false;
    }

    const int32_t draw_w = std::min(decoded_w, disp_w);
    const int32_t draw_h = std::min(decoded_h, disp_h);
    const int32_t src_x0 = (decoded_w > draw_w) ? ((decoded_w - draw_w) / 2) : 0;
    const int32_t src_y0 = (decoded_h > draw_h) ? ((decoded_h - draw_h) / 2) : 0;
    const int32_t dst_x0 = (disp_w > draw_w) ? ((disp_w - draw_w) / 2) : 0;
    const int32_t dst_y0 = (disp_h > draw_h) ? ((disp_h - draw_h) / 2) : 0;

    if (draw_w == decoded_w && draw_h == decoded_h) {
        display.pushImage(dst_x0, dst_y0, decoded_w, decoded_h, image_data, lgfx::color_depth_t::grayscale_1bit,
                          kGray1Palette);
        return true;
    }

    const uint64_t crop_row_bytes = (static_cast<uint64_t>(draw_w) + 7u) / 8u;
    const uint64_t crop_size64 = crop_row_bytes * static_cast<uint64_t>(draw_h);
    if (crop_size64 > size_max) {
        ESP_LOGE(kXtgTag, "XTG: crop buffer too large");
        return false;
    }

    std::vector<uint8_t> crop_buf(static_cast<size_t>(crop_size64), 0xFF);
    for (int32_t yy = 0; yy < draw_h; ++yy) {
        for (int32_t xx = 0; xx < draw_w; ++xx) {
            const uint8_t v = get_pixel_1bpp(image_data, decoded_w, src_x0 + xx, src_y0 + yy);
            set_pixel_1bpp(crop_buf.data(), draw_w, xx, yy, v);
        }
    }

    display.pushImage(dst_x0, dst_y0, draw_w, draw_h, crop_buf.data(), lgfx::color_depth_t::grayscale_1bit,
                      kGray1Palette);
    return true;
}

}
