#include "fonts/vlw_font.h"

#include <algorithm>
#include <limits>

namespace {

/** @brief Read a big-endian 32-bit integer from a VLW byte stream. */
uint32_t read_be32(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | (uint32_t)ptr[3];
}

/** @brief Return the absolute value of a signed 32-bit metric. */
int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

/** @brief Write a plain parse error string when the caller requested one. */
bool assign_error(std::string *out_error, const char *message)
{
    if (out_error) {
        *out_error = message ? message : "unknown error";
    }
    return false;
}

/** @brief Write a formatted parse error string when the caller requested one. */
bool assign_errorf(std::string *out_error, const std::string &message)
{
    if (out_error) {
        *out_error = message;
    }
    return false;
}

} // namespace

/** @brief Parse and validate a copied VLW payload into immutable glyph tables. */
std::shared_ptr<VlwFont> VlwFont::CreateCopy(const uint8_t *ptr, size_t len, const char *debug_name, std::string *out_error)
{
    if (!ptr) {
        assign_error(out_error, "font bytes pointer is null");
        return nullptr;
    }
    if (len < 24) {
        assign_error(out_error, "font too small for VLW header");
        return nullptr;
    }

    auto font = std::make_shared<VlwFont>();
    font->debug_name_ = debug_name ? debug_name : "vlw";
    font->bytes_.assign(ptr, ptr + len);

    const uint8_t *bytes = font->bytes_.data();
    const uint32_t glyph_count_u32 = read_be32(bytes + 0);
    const uint32_t y_advance_u32 = read_be32(bytes + 8);
    const int32_t ascent_i32 = (int32_t)read_be32(bytes + 16);
    const int32_t descent_i32 = (int32_t)read_be32(bytes + 20);

    if (glyph_count_u32 == 0 || glyph_count_u32 > (uint32_t)std::numeric_limits<uint16_t>::max()) {
        assign_error(out_error, "invalid VLW glyph count");
        return nullptr;
    }
    if (y_advance_u32 > (uint32_t)std::numeric_limits<int16_t>::max()) {
        assign_error(out_error, "invalid VLW line height");
        return nullptr;
    }

    const size_t glyph_count = (size_t)glyph_count_u32;
    const size_t table_bytes = glyph_count * 28u;
    if (table_bytes / 28u != glyph_count || 24u + table_bytes > len) {
        assign_error(out_error, "VLW glyph table exceeds font length");
        return nullptr;
    }

    const int16_t ascent = (int16_t)std::min<int32_t>(abs_i32(ascent_i32), std::numeric_limits<int16_t>::max());
    const int16_t descent = (int16_t)std::min<int32_t>(abs_i32(descent_i32), std::numeric_limits<int16_t>::max());
    int32_t computed_y_advance = std::max<int32_t>((int32_t)y_advance_u32, (int32_t)ascent + (int32_t)descent);

    font->metrics_.glyph_count = (uint16_t)glyph_count_u32;
    font->metrics_.ascent = ascent;
    font->metrics_.descent = descent;
    font->metrics_.max_ascent = ascent;
    font->metrics_.max_descent = descent;
    font->metrics_.space_width = (uint16_t)std::max<int32_t>(0, computed_y_advance * 2 / 7);
    font->metrics_.line_height = (int16_t)std::min<int32_t>(computed_y_advance, std::numeric_limits<int16_t>::max());

    font->glyphs_.reserve(glyph_count);
    font->glyph_lookup_.reserve(glyph_count);

    uint32_t bitmap_offset = (uint32_t)(24u + table_bytes);
    for (size_t i = 0; i < glyph_count; ++i) {
        const uint8_t *entry = bytes + 24u + (i * 28u);

        const uint32_t unicode_u32 = read_be32(entry + 0);
        const uint32_t height_u32 = read_be32(entry + 4);
        const uint32_t width_u32 = read_be32(entry + 8);
        const uint32_t x_advance_raw = read_be32(entry + 12);
        const int32_t y_delta_i32 = (int32_t)read_be32(entry + 16);
        const int32_t x_delta_i32 = (int32_t)read_be32(entry + 20);

        if (unicode_u32 > 0xFFFFu) {
            assign_errorf(out_error, "VLW glyph unicode exceeds BMP at glyph index " + std::to_string(i));
            return nullptr;
        }
        if (width_u32 > (uint32_t)std::numeric_limits<uint16_t>::max()
            || height_u32 > (uint32_t)std::numeric_limits<uint16_t>::max()
            || x_advance_raw > (uint32_t)std::numeric_limits<uint16_t>::max()) {
            assign_errorf(out_error, "VLW glyph dimensions overflow at glyph index " + std::to_string(i));
            return nullptr;
        }
        if (y_delta_i32 < (int32_t)std::numeric_limits<int16_t>::min()
            || y_delta_i32 > (int32_t)std::numeric_limits<int16_t>::max()
            || x_delta_i32 < (int32_t)std::numeric_limits<int8_t>::min()
            || x_delta_i32 > (int32_t)std::numeric_limits<int8_t>::max()) {
            assign_errorf(out_error, "VLW glyph deltas overflow at glyph index " + std::to_string(i));
            return nullptr;
        }

        const uint64_t glyph_bytes = (uint64_t)width_u32 * (uint64_t)height_u32;
        if (glyph_bytes > (uint64_t)std::numeric_limits<uint32_t>::max()) {
            assign_errorf(out_error, "VLW glyph bitmap is too large at glyph index " + std::to_string(i));
            return nullptr;
        }
        if ((uint64_t)bitmap_offset + glyph_bytes > (uint64_t)len) {
            assign_errorf(out_error, "VLW glyph bitmap exceeds font length at glyph index " + std::to_string(i));
            return nullptr;
        }

        VlwGlyph glyph = {};
        glyph.codepoint = (uint16_t)unicode_u32;
        glyph.width = (uint16_t)width_u32;
        glyph.height = (uint16_t)height_u32;
        glyph.x_advance = (uint16_t)x_advance_raw;
        glyph.y_delta = (int16_t)y_delta_i32;
        glyph.x_delta = (int8_t)x_delta_i32;
        glyph.bitmap_offset = bitmap_offset;

        font->glyph_lookup_[glyph.codepoint] = font->glyphs_.size();
        font->glyphs_.push_back(glyph);
        bitmap_offset += (uint32_t)glyph_bytes;

        if ((glyph.codepoint > 0xFFu || ((glyph.codepoint > 0x20u) && (glyph.codepoint < 0xA0u) && (glyph.codepoint != 0x7Fu)))
            && glyph.codepoint != 0x3000u) {
            font->metrics_.max_ascent = std::max<int16_t>(font->metrics_.max_ascent, glyph.y_delta);
            font->metrics_.max_descent =
                std::max<int16_t>(font->metrics_.max_descent, (int16_t)((int32_t)glyph.height - (int32_t)glyph.y_delta));
        }
    }

    font->metrics_.line_height = (int16_t)std::min<int32_t>(
        (int32_t)font->metrics_.max_ascent + (int32_t)font->metrics_.max_descent,
        std::numeric_limits<int16_t>::max());
    font->valid_ = true;
    return font;
}

/** @brief Return the precomputed aggregate metrics for this font. */
const VlwMetrics &VlwFont::metrics() const
{
    return metrics_;
}

/** @brief Find a glyph record by its Unicode codepoint. */
const VlwGlyph *VlwFont::FindGlyph(uint16_t codepoint) const
{
    auto it = glyph_lookup_.find(codepoint);
    if (it == glyph_lookup_.end()) {
        return nullptr;
    }
    return &glyphs_[it->second];
}

/** @brief Return the start of a glyph's bitmap inside the copied VLW payload. */
const uint8_t *VlwFont::GlyphBitmap(const VlwGlyph &glyph) const
{
    if (glyph.bitmap_offset >= bytes_.size()) {
        return nullptr;
    }
    return bytes_.data() + glyph.bitmap_offset;
}

/** @brief Report whether the font finished parsing successfully. */
bool VlwFont::IsValid() const
{
    return valid_;
}

/** @brief Return the human-readable debug name attached to this font. */
const char *VlwFont::debug_name() const
{
    return debug_name_.c_str();
}
