#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/** @brief Metadata for one glyph entry parsed from a VLW font. */
struct VlwGlyph {
    /** Unicode codepoint for the glyph. */
    uint16_t codepoint = 0;
    /** Source bitmap width in pixels. */
    uint16_t width = 0;
    /** Source bitmap height in pixels. */
    uint16_t height = 0;
    /** Cursor advance after drawing this glyph. */
    uint16_t x_advance = 0;
    /** Glyph ascent offset relative to the font baseline. */
    int16_t y_delta = 0;
    /** Horizontal bitmap offset relative to the text cursor. */
    int8_t x_delta = 0;
    /** Byte offset of the glyph bitmap inside the copied VLW payload. */
    uint32_t bitmap_offset = 0;
};

/** @brief Aggregate metrics derived from a VLW font file. */
struct VlwMetrics {
    /** Number of glyph records in the font. */
    uint16_t glyph_count = 0;
    /** Nominal ascent from the VLW header. */
    int16_t ascent = 0;
    /** Nominal descent from the VLW header. */
    int16_t descent = 0;
    /** Largest ascent observed across drawable glyphs. */
    int16_t max_ascent = 0;
    /** Largest descent observed across drawable glyphs. */
    int16_t max_descent = 0;
    /** Line height used by the FastEPD VLW renderer. */
    int16_t line_height = 0;
    /** Fallback advance used for spaces and missing glyphs. */
    uint16_t space_width = 0;
};

/** @brief Parsed VLW font data with immutable glyph and bitmap lookup tables. */
class VlwFont {
public:
    /**
     * @brief Validate and copy a VLW payload into an immutable parsed font object.
     * @param ptr Source VLW bytes.
     * @param len Length of @p ptr in bytes.
     * @param debug_name Human-readable name used in logs and errors.
     * @param out_error Optional parse error output.
     * @return Parsed font on success, otherwise `nullptr`.
     */
    static std::shared_ptr<VlwFont> CreateCopy(
        const uint8_t *ptr,
        size_t len,
        const char *debug_name,
        std::string *out_error = nullptr);

    /** @brief Return aggregate metrics for the parsed font. */
    const VlwMetrics &metrics() const;
    /** @brief Look up a glyph by Unicode codepoint. */
    const VlwGlyph *FindGlyph(uint16_t codepoint) const;
    /** @brief Return a pointer to the copied bitmap data for a glyph. */
    const uint8_t *GlyphBitmap(const VlwGlyph &glyph) const;
    /** @brief True when parsing succeeded and the font data is internally consistent. */
    bool IsValid() const;
    /** @brief Human-readable font name used for diagnostics. */
    const char *debug_name() const;

private:
    VlwMetrics metrics_ = {};
    std::string debug_name_;
    std::vector<uint8_t> bytes_;
    std::vector<VlwGlyph> glyphs_;
    std::unordered_map<uint16_t, size_t> glyph_lookup_;
    bool valid_ = false;
};
