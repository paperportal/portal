#pragma once

#include <cstdint>

#include <FastEPD.h>

#include "fonts/vlw_font.h"

/** @brief App-visible text attributes consumed by the FastEPD VLW renderer. */
struct FastEpdVlwTextState {
    /** Horizontal text scale factor. */
    float size_x = 1.0f;
    /** Vertical text scale factor. */
    float size_y = 1.0f;
    /** Datum/alignment code matching the public display API. */
    int32_t datum = 0;
    /** Whether horizontal wrapping is requested. */
    bool wrap_x = false;
    /** Whether vertical wrapping is requested. */
    bool wrap_y = false;
    /** Whether input strings should be decoded as UTF-8. */
    bool utf8_enabled = false;
    /** Whether CP437 remapping should stay enabled for single-byte text. */
    bool cp437_enabled = false;
    /** Whether scroll-on-overflow behavior is requested. */
    bool scroll_enabled = false;
    /** Foreground text color in RGB888. */
    int32_t fg_rgb888 = 0x000000;
    /** Background text color in RGB888. */
    int32_t bg_rgb888 = 0xFFFFFF;
    /** Whether glyph rendering should paint the background color. */
    bool use_bg = false;
};

/**
 * @brief Measure the rendered width of a string using the active VLW text state.
 * @param font Parsed VLW font.
 * @param state FastEPD text state to apply.
 * @param text UTF-8 or single-byte input string.
 * @param out_width Output width in logical pixels.
 * @return `kWasmOk` on success.
 */
int32_t MeasureTextWidth(const VlwFont &font, const FastEpdVlwTextState &state, const char *text, int32_t *out_width);
/**
 * @brief Compute the current scaled line height for a VLW font.
 * @param font Parsed VLW font.
 * @param state FastEPD text state to apply.
 * @param out_height Output height in logical pixels.
 * @return `kWasmOk` on success.
 */
int32_t CurrentFontHeight(const VlwFont &font, const FastEpdVlwTextState &state, int32_t *out_height);
/**
 * @brief Render a string through the FastEPD framebuffer using VLW glyph bitmaps.
 * @param epd Target FastEPD instance.
 * @param font Parsed VLW font.
 * @param state FastEPD text state to apply.
 * @param text UTF-8 or single-byte input string.
 * @param x Logical X coordinate interpreted according to `state.datum`.
 * @param y Logical Y coordinate interpreted according to `state.datum`.
 * @param out_width Optional rendered width output in logical pixels.
 * @return `kWasmOk` on success.
 */
int32_t DrawString(
    FASTEPD &epd,
    const VlwFont &font,
    const FastEpdVlwTextState &state,
    const char *text,
    int32_t x,
    int32_t y,
    int32_t *out_width);
