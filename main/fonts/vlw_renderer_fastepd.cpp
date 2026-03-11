#include "fonts/vlw_renderer_fastepd.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "wasm/api/errors.h"

namespace {

/** @brief Glyph data normalized for FastEPD measurement and rendering. */
struct PreparedGlyph {
    uint16_t codepoint = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t x_advance = 0;
    int16_t y_delta = 0;
    int8_t x_delta = 0;
    const uint8_t *bitmap = nullptr;
};

/** @brief Prepared glyph run plus aggregate width for one input string. */
struct PreparedText {
    std::vector<PreparedGlyph> glyphs;
    int32_t initial_offset = 0;
    int32_t width = 0;
};

/** @brief Decode one UTF-8 code unit sequence into a BMP codepoint. */
uint16_t decode_utf8_char(const char **cursor)
{
    const uint8_t c0 = (uint8_t)**cursor;
    if ((c0 & 0x80u) == 0) {
        (*cursor)++;
        return c0;
    }

    if ((c0 & 0xE0u) == 0xC0u && (uint8_t)(*cursor)[1] != 0) {
        const uint16_t code = (uint16_t)(((c0 & 0x1Fu) << 6) | ((uint8_t)(*cursor)[1] & 0x3Fu));
        *cursor += 2;
        return code;
    }

    if ((c0 & 0xF0u) == 0xE0u && (uint8_t)(*cursor)[1] != 0 && (uint8_t)(*cursor)[2] != 0) {
        const uint16_t code = (uint16_t)(((c0 & 0x0Fu) << 12) | (((uint8_t)(*cursor)[1] & 0x3Fu) << 6)
            | ((uint8_t)(*cursor)[2] & 0x3Fu));
        *cursor += 3;
        return code;
    }

    (*cursor)++;
    return c0;
}

/** @brief Decode the next codepoint using the active UTF-8 or CP437 text mode. */
uint16_t decode_next_codepoint(const char **cursor, const FastEpdVlwTextState &state)
{
    if (state.utf8_enabled) {
        return decode_utf8_char(cursor);
    }

    uint16_t codepoint = (uint8_t)**cursor;
    (*cursor)++;
    if (!state.cp437_enabled && codepoint >= 176u) {
        codepoint++;
    }
    return codepoint;
}

/** @brief Convert a float scale factor into 16.16 fixed-point. */
int32_t scale_fixed(float value)
{
    const float clamped = value > 0.0f ? value : 1.0f;
    return (int32_t)lroundf(clamped * 65536.0f);
}

/** @brief Scale a positive dimension using 16.16 fixed-point math. */
int32_t scale_dim(uint16_t value, int32_t scale)
{
    if (value == 0) {
        return 0;
    }
    int32_t scaled = (int32_t)(((int64_t)value * (int64_t)scale) >> 16);
    return scaled > 0 ? scaled : 1;
}

/** @brief Resolve one codepoint to either a real glyph or a width-only fallback. */
PreparedGlyph prepare_glyph(const VlwFont &font, uint16_t codepoint)
{
    if (codepoint == 0x20u) {
        PreparedGlyph glyph = {};
        glyph.codepoint = codepoint;
        glyph.x_advance = font.metrics().space_width;
        glyph.width = font.metrics().space_width;
        return glyph;
    }

    const VlwGlyph *glyph = font.FindGlyph(codepoint);
    if (!glyph) {
        PreparedGlyph missing = {};
        missing.codepoint = codepoint;
        missing.x_advance = font.metrics().space_width;
        missing.width = font.metrics().space_width;
        return missing;
    }

    PreparedGlyph prepared = {};
    prepared.codepoint = glyph->codepoint;
    prepared.width = glyph->width;
    prepared.height = glyph->height;
    prepared.x_advance = glyph->x_advance;
    prepared.y_delta = glyph->y_delta;
    prepared.x_delta = glyph->x_delta;
    prepared.bitmap = font.GlyphBitmap(*glyph);
    return prepared;
}

/** @brief Decode text and precompute glyph positions needed for measure and draw. */
PreparedText prepare_text(const VlwFont &font, const FastEpdVlwTextState &state, const char *text)
{
    PreparedText prepared = {};
    if (!text || text[0] == '\0') {
        return prepared;
    }

    const int32_t sx = scale_fixed(state.size_x);
    int32_t left = 0;
    int32_t right = 0;

    const char *cursor = text;
    while (*cursor) {
        const PreparedGlyph glyph = prepare_glyph(font, decode_next_codepoint(&cursor, state));
        prepared.glyphs.push_back(glyph);

        const int32_t scaled_offset = ((int32_t)glyph.x_delta * sx) >> 16;
        if (left == 0 && right == 0 && glyph.x_delta < 0) {
            left = right = -scaled_offset;
            prepared.initial_offset = -scaled_offset;
        }

        const int32_t scaled_advance = scale_dim(glyph.x_advance, sx);
        const int32_t scaled_width = scale_dim(glyph.width, sx);
        right = left + std::max<int32_t>(scaled_advance, scaled_width + scaled_offset);
        left += scaled_advance;
    }

    prepared.width = right;
    return prepared;
}

/** @brief Convert RGB888 to 8-bit grayscale for FastEPD blending. */
uint8_t rgb888_to_gray8(int32_t rgb888)
{
    const uint32_t raw = (uint32_t)rgb888;
    const uint8_t r = (uint8_t)((raw >> 16) & 0xFFu);
    const uint8_t g = (uint8_t)((raw >> 8) & 0xFFu);
    const uint8_t b = (uint8_t)(raw & 0xFFu);
    return (uint8_t)((r * 77u + g * 150u + b * 29u + 128u) >> 8);
}

/** @brief Quantize an 8-bit grayscale value into the current FastEPD mode. */
uint8_t gray8_to_epd_color(uint8_t gray, int32_t mode)
{
    if (mode == BB_MODE_1BPP) {
        return (gray >= 128u) ? (uint8_t)BBEP_WHITE : (uint8_t)BBEP_BLACK;
    }
    if (mode == BB_MODE_2BPP) {
        return (uint8_t)(((uint16_t)gray * 3u + 127u) / 255u);
    }
    uint8_t color = (uint8_t)((gray + 8u) >> 4);
    return color > 15u ? 15u : color;
}

/** @brief Expand a FastEPD framebuffer pixel back into 8-bit grayscale. */
uint8_t epd_color_to_gray8(uint8_t color, int32_t mode)
{
    if (mode == BB_MODE_1BPP) {
        return color == BBEP_WHITE ? 255u : 0u;
    }
    if (mode == BB_MODE_2BPP) {
        return (uint8_t)((color * 255u) / 3u);
    }
    return (uint8_t)(color * 17u);
}

/** @brief Normalize the reported panel rotation to the four logical orientations. */
int32_t logical_rotation(FASTEPD &epd)
{
    const int rotation = epd.getRotation();
    switch (rotation) {
    case 0:
    case 90:
    case 180:
    case 270:
        return rotation;
    default:
        return 0;
    }
}

/** @brief Read the existing framebuffer pixel so glyph alpha can blend over it. */
uint8_t read_epd_pixel(FASTEPD &epd, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= epd.width() || y >= epd.height()) {
        return 0;
    }

    uint8_t *buffer = epd.currentBuffer();
    if (!buffer) {
        return 0;
    }

    const int32_t mode = epd.getMode();
    const int32_t rotation = logical_rotation(epd);
    const int32_t logical_width = epd.width();
    const int32_t logical_height = epd.height();
    const int32_t native_width = (rotation == 0 || rotation == 180) ? logical_width : logical_height;

    if (mode == BB_MODE_1BPP) {
        const int32_t pitch = (native_width + 7) >> 3;
        int32_t index = 0;
        uint8_t mask = 0;
        switch (rotation) {
        case 0:
            index = (x >> 3) + (y * pitch);
            mask = (uint8_t)(0x80u >> (x & 7));
            break;
        case 90:
            index = (y >> 3) + ((logical_width - 1 - x) * pitch);
            mask = (uint8_t)(0x80u >> (y & 7));
            break;
        case 180:
            index = ((logical_width - 1 - x) >> 3) + ((logical_height - 1 - y) * pitch);
            mask = (uint8_t)(1u << (x & 7));
            break;
        default:
            index = ((logical_height - 1 - y) >> 3) + (x * pitch);
            mask = (uint8_t)(1u << (y & 7));
            break;
        }
        return (buffer[index] & mask) ? (uint8_t)BBEP_WHITE : (uint8_t)BBEP_BLACK;
    }

    if (mode == BB_MODE_2BPP) {
        const int32_t pitch = native_width >> 2;
        int32_t index = 0;
        int shift = 0;
        switch (rotation) {
        case 0:
            index = (x >> 2) + (y * pitch);
            shift = (3 - (x & 3)) * 2;
            break;
        case 90:
            index = (y >> 2) + ((logical_width - 1 - x) * pitch);
            shift = (3 - (y & 3)) * 2;
            break;
        case 180:
            index = ((logical_width - 1 - x) >> 2) + ((logical_height - 1 - y) * pitch);
            shift = (x & 3) * 2;
            break;
        default:
            index = ((logical_height - 1 - y) >> 2) + (x * pitch);
            shift = (y & 3) * 2;
            break;
        }
        return (uint8_t)((buffer[index] >> shift) & 0x03u);
    }

    const int32_t pitch = native_width >> 1;
    int32_t index = 0;
    bool low_nibble = false;
    switch (rotation) {
    case 0:
        index = (x >> 1) + (y * pitch);
        low_nibble = (x & 1) != 0;
        break;
    case 90:
        index = (y >> 1) + ((logical_width - 1 - x) * pitch);
        low_nibble = (y & 1) != 0;
        break;
    case 180:
        index = ((logical_width - 1 - x) >> 1) + ((logical_height - 1 - y) * pitch);
        low_nibble = (x & 1) == 0;
        break;
    default:
        index = ((logical_height - 1 - y) >> 1) + (x * pitch);
        low_nibble = (y & 1) == 0;
        break;
    }

    const uint8_t value = buffer[index];
    return low_nibble ? (uint8_t)(value & 0x0Fu) : (uint8_t)((value >> 4) & 0x0Fu);
}

/** @brief Rasterize one glyph bitmap into the framebuffer with grayscale blending. */
void blend_glyph(
    FASTEPD &epd,
    const PreparedGlyph &glyph,
    const VlwFont &font,
    const FastEpdVlwTextState &state,
    int32_t cursor_x,
    int32_t line_top_y)
{
    if (!glyph.bitmap || glyph.width == 0 || glyph.height == 0) {
        return;
    }

    const int32_t sx = scale_fixed(state.size_x);
    const int32_t sy = scale_fixed(state.size_y);
    const int32_t scaled_width = scale_dim(glyph.width, sx);
    const int32_t scaled_height = scale_dim(glyph.height, sy);
    const int32_t draw_x = cursor_x + (((int32_t)glyph.x_delta * sx) >> 16);
    const int32_t draw_y = line_top_y + (((int32_t)font.metrics().max_ascent - (int32_t)glyph.y_delta) * sy >> 16);
    const int32_t mode = epd.getMode();
    const uint8_t fg_gray = rgb888_to_gray8(state.fg_rgb888);
    const uint8_t bg_gray = rgb888_to_gray8(state.bg_rgb888);

    for (int32_t dy = 0; dy < scaled_height; ++dy) {
        const int32_t py = draw_y + dy;
        if (py < 0 || py >= epd.height()) {
            continue;
        }
        const int32_t src_y = std::min<int32_t>((dy * (int32_t)glyph.height) / scaled_height, (int32_t)glyph.height - 1);
        for (int32_t dx = 0; dx < scaled_width; ++dx) {
            const int32_t px = draw_x + dx;
            if (px < 0 || px >= epd.width()) {
                continue;
            }

            const int32_t src_x = std::min<int32_t>((dx * (int32_t)glyph.width) / scaled_width, (int32_t)glyph.width - 1);
            const uint8_t alpha = glyph.bitmap[src_y * glyph.width + src_x];
            if (alpha == 0u) {
                continue;
            }

            uint8_t base_gray = bg_gray;
            if (!state.use_bg) {
                base_gray = epd_color_to_gray8(read_epd_pixel(epd, px, py), mode);
            }

            const uint16_t blended = (uint16_t)base_gray * (uint16_t)(255u - alpha) + (uint16_t)fg_gray * (uint16_t)alpha;
            const uint8_t gray = (uint8_t)((blended + 127u) / 255u);
            epd.drawPixelFast(px, py, gray8_to_epd_color(gray, mode));
        }
    }
}

} // namespace

/** @brief Measure the rendered width of a text run using VLW metrics. */
int32_t MeasureTextWidth(const VlwFont &font, const FastEpdVlwTextState &state, const char *text, int32_t *out_width)
{
    if (!out_width) {
        return kWasmErrInternal;
    }

    *out_width = prepare_text(font, state, text).width;
    return kWasmOk;
}

/** @brief Compute the current scaled VLW line height. */
int32_t CurrentFontHeight(const VlwFont &font, const FastEpdVlwTextState &state, int32_t *out_height)
{
    if (!out_height) {
        return kWasmErrInternal;
    }

    *out_height = scale_dim((uint16_t)font.metrics().line_height, scale_fixed(state.size_y));
    return kWasmOk;
}

/** @brief Draw a text run at the requested datum using VLW glyph bitmaps. */
int32_t DrawString(
    FASTEPD &epd,
    const VlwFont &font,
    const FastEpdVlwTextState &state,
    const char *text,
    int32_t x,
    int32_t y,
    int32_t *out_width)
{
    if (!out_width) {
        return kWasmErrInternal;
    }

    const PreparedText prepared = prepare_text(font, state, text);
    const int32_t sy = scale_fixed(state.size_y);
    const int32_t cheight = scale_dim((uint16_t)font.metrics().line_height, sy);
    const int32_t baseline = scale_dim((uint16_t)font.metrics().max_ascent, sy);
    int32_t draw_x = x;
    int32_t draw_y = y;

    if (state.datum & 4) {
        draw_y -= cheight >> 1;
    } else if (state.datum & 8) {
        draw_y -= cheight;
    } else if (state.datum & 16) {
        draw_y -= baseline;
    }

    if ((state.datum & 3) == 1) {
        draw_x -= prepared.width >> 1;
    } else if ((state.datum & 3) == 2) {
        draw_x -= prepared.width;
    }

    if (state.use_bg && prepared.width > 0 && cheight > 0) {
        epd.fillRect(draw_x, draw_y, prepared.width, cheight, gray8_to_epd_color(rgb888_to_gray8(state.bg_rgb888), epd.getMode()));
    }

    int32_t cursor_x = draw_x + prepared.initial_offset;
    for (const PreparedGlyph &glyph : prepared.glyphs) {
        blend_glyph(epd, glyph, font, state, cursor_x, draw_y);
        cursor_x += scale_dim(glyph.x_advance, scale_fixed(state.size_x));
    }

    *out_width = prepared.width;
    return kWasmOk;
}
