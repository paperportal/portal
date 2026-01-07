#pragma once

#include <cstddef>
#include <cstdint>

#include <vector>

class LGFX_M5PaperS3;

/**
 * @file xteink_image_utils.h
 * @brief Decode an XTH image into a packed 2bpp (4-level grayscale) buffer.
 *
 * ## XTH file format (as handled by this project)
 *
 * The decoder expects an XTH image to be laid out as a small fixed header followed by two
 * uncompressed 1bpp bitplanes.
 *
 * ### Header (22 bytes total)
 *
 * All multi-byte fields are little-endian.
 *
 * | Offset | Size | Field            | Notes |
 * |--------|------|------------------|-------|
 * | 0x00   | 4    | magic            | Must be ASCII `"XTH\\0"` (bytes `58 54 48 00`) |
 * | 0x04   | 2    | width            | `uint16_le`, must be > 0 |
 * | 0x06   | 2    | height           | `uint16_le`, must be > 0 |
 * | 0x08   | 1    | color_mode       | Must be `0` (only mode supported here) |
 * | 0x09   | 1    | compression      | Must be `0` (no compression) |
 * | 0x0A   | 4    | data_size        | `uint32_le`, number of bytes after the header (informational) |
 * | 0x0E   | 8    | reserved/ignored | Not interpreted by this decoder |
 *
 * ### Pixel data
 *
 * After the 22-byte header, there are two planes:
 * - Plane 1: `ceil(width * height / 8)` bytes
 * - Plane 2: `ceil(width * height / 8)` bytes
 *
 * Bits are stored MSB-first within each byte (bit mask `0x80 >> (p & 7)`).
 *
 * The bit index `p` for a pixel at `(x, y)` is computed using XTH's column-major ordering and
 * right-to-left column scan:
 *
 * - `col = (width - 1) - x`
 * - `p = col * height + y`
 *
 * For a given `p`, the 2-bit pixel value is:
 * - `b1 = bit from plane1 at p`
 * - `b2 = bit from plane2 at p`
 * - `pixel = (b1 << 1) | b2` (range 0..3)
 *
 * This project assumes the XTH pixel levels map as `0=white .. 3=black`.
 *
 * ### Output buffer format
 *
 * `out2bpp` is a packed 2bpp, row-major (left-to-right, top-to-bottom) buffer.
 * Four pixels are packed into one byte:
 * - pixel 0 -> bits 7..6
 * - pixel 1 -> bits 5..4
 * - pixel 2 -> bits 3..2
 * - pixel 3 -> bits 1..0
 *
 * `out2bpp.size()` will be `ceil(width * height / 4)`.
 */
/**
 * @brief Convert an XTH image buffer to a packed 2bpp image.
 *
 * @param xth Pointer to the XTH file bytes.
 * @param xth_size Size of the XTH buffer in bytes.
 * @param[out] out2bpp Destination buffer for packed 2bpp pixels (cleared/resized by this function).
 * @param[out] out_w Output width in pixels.
 * @param[out] out_h Output height in pixels.
 * @return true on success, false if the input is invalid or unsupported.
 */
bool convertXth(const uint8_t *xth, size_t xth_size, std::vector<uint8_t> &out2bpp, int32_t &out_w, int32_t &out_h);

/**
 * @brief Decode an XTH image and draw it centered on the display.
 *
 * The decoded image is center-cropped to the display size when it is larger than
 * the destination.
 *
 * @param display Target display.
 * @param xth Pointer to XTH bytes.
 * @param xth_size Size of XTH buffer in bytes.
 * @return true on success, false on decode/draw failure.
 */
bool drawXth(LGFX_M5PaperS3 &display, const uint8_t *xth, size_t xth_size);

/**
 * @brief Decode an XTG (1bpp monochrome) image and draw it centered on the display.
 *
 * The decoded image is center-cropped to the display size when it is larger than
 * the destination.
 *
 * @param display Target display.
 * @param xtg Pointer to XTG bytes.
 * @param xtg_size Size of XTG buffer in bytes.
 * @return true on success, false on decode/draw failure.
 */
bool drawXtg(LGFX_M5PaperS3 &display, const uint8_t *xtg, size_t xtg_size);
