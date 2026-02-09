# VLW Font Format Specification

## Overview
VLW (Processing) is a **bitmap font format** where all multi-byte values are stored in **big-endian** byte order.

## File Structure

```
+------------------+
| Header (24 bytes)|
+------------------+
| Glyph Table      |
| (28 bytes each)  |
+------------------+
| Bitmap Data      |
| (variable)       |
+------------------+
```

---

## Header (24 bytes)

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | uint32 BE | `glyphCount` | Number of glyphs in file |
| 0x04 | 4 | uint32 BE | `version` | VLW encoder version (unused) |
| 0x08 | 4 | uint32 BE | `yAdvance` | Font size in points (not pixels) |
| 0x0C | 4 | uint32 BE | `reserved` | Unused |
| 0x10 | 4 | int32 BE | `ascent` | Top of "d" (distance from baseline to top) |
| 0x14 | 4 | int32 BE | `descent` | Bottom of "p" (distance from baseline to bottom) |

---

## Glyph Table Entry (28 bytes per glyph)

Stored at offset `24 + (glyphIndex × 28)`.

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | uint32 BE | `unicode` | Unicode code point (16-bit valid, stored in 32 bits) |
| 0x04 | 4 | uint32 BE | `height` | Glyph bitmap height in pixels |
| 0x08 | 4 | uint32 BE | `width` | Glyph bitmap width in pixels (stored as 32-bit, only low 8 bits used) |
| 0x0C | 4 | uint32 BE | `xAdvance` | Horizontal advance to move cursor (stored as 32-bit, only low 8 bits used) |
| 0x10 | 4 | int16 BE | `yDelta` | Y offset from baseline to top of bitmap (signed) |
| 0x14 | 4 | int8 BE | `xDelta` | X offset from cursor to left edge of bitmap (signed, stored in 32 bits) |
| 0x18 | 4 | uint32 BE | `reserved` | Unused |

---

## Bitmap Data

- **Starts at:** `24 + (glyphCount × 28)`
- **Format:** Raw 8-bit grayscale alpha values (1 byte per pixel)
- **Layout:** Row-major order within each glyph
- **Glyph access:** Use stored bitmap pointers calculated during load

The bitmap pointer for glyph *i* is:
```
bitmapOffset[i] = 24 + (glyphCount × 28) + Σ(width[j] × height[j]) for j = 0 to i-1
```

---

## Byte Order (Endianness)

All multi-byte values use **big-endian** byte order. The `getSwap32()` function converts:

```cpp
uint32_t getSwap32(uint32_t c) {
  c = (c >> 16) + (c << 16);
  return ((c >> 8) & 0xFF00FF) + ((c & 0xFF00FF) << 8);
}
```

---

## Space Character

The space character (U+0020) is **not stored** in the glyph table. Its width is calculated as:
```
spaceWidth = yAdvance × 2 / 7
```

---

## Special Code Points

- **U+0020 (space):** Handled separately, uses calculated `spaceWidth`
- **U+3000 (ideographic space):** Excluded from ascent/descent calculations
- **U+007F (DEL):** Treated specially in metric calculations
- **U+0021-U+00FF:** May be excluded from certain metric calculations

---

## Rendering Notes

1. **Baseline:** Text renders from baseline; `yDelta` is offset from baseline to bitmap top
2. **Cursor positioning:** `xDelta` can be negative (glyph extends left of cursor)
3. **Max metrics:** `maxAscent` and `maxDescent` are recalculated from actual glyph data (excluding U+3000)
4. **Bitmap format:** Single-byte alpha (0-255), typically rendered with anti-aliasing

---

## Source Code Reference

Based on analysis of LovyanGFX implementation:
- `src/lgfx/v1/lgfx_fonts.cpp` - `VLWfont::loadFont()` (line 791)
- `src/lgfx/v1/LGFXBase.cpp` - `LGFXBase::loadFont()` (line 2451)
