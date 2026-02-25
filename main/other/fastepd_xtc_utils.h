/**
 * @file fastepd_xtc_utils.h
 * @brief XTG/XTH parsing and blitting utilities used by `drawXtg()` / `drawXth()`.
 *
 * This header intentionally contains `static inline` helpers to keep the XTG/XTH
 * draw implementation in a single translation unit while enabling aggressive
 * inlining of the bit-manipulation hot paths.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fastepd_xtc_utils {

/// @brief Size in bytes of the XTG/XTH header.
constexpr size_t kXtxHeaderSize = 22;

/// @brief Magic value for an XTG header (`"XTG\\0"` interpreted as little-endian u32).
constexpr uint32_t kXtgMagic = 0x00475458; // "XTG\0" as little-endian u32
/// @brief Magic value for an XTH header (`"XTH\\0"` interpreted as little-endian u32).
constexpr uint32_t kXthMagic = 0x00485458; // "XTH\0" as little-endian u32

/**
 * @brief Parsed XTG/XTH header fields.
 *
 * This struct represents the normalized header fields used by both formats.
 */
struct XtxImageHeader {
    uint16_t width; ///< Image width in pixels.
    uint16_t height; ///< Image height in pixels.
    uint8_t color_mode; ///< Color mode (currently only `0` supported by callers).
    uint8_t compression; ///< Compression mode (currently only `0` supported by callers).
    uint32_t data_size; ///< Payload size in bytes (excluding the header).
    uint8_t md5_8[8]; ///< First 8 bytes of an MD5 checksum (as stored in the file).
};

/**
 * @brief Load a little-endian 16-bit value from a byte buffer.
 * @param p Pointer to at least 2 bytes.
 * @return Parsed value.
 */
static inline uint16_t loadLeU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

/**
 * @brief Load a little-endian 32-bit value from a byte buffer.
 * @param p Pointer to at least 4 bytes.
 * @return Parsed value.
 */
static inline uint32_t loadLeU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

/**
 * @brief Parse a common XTG/XTH header.
 *
 * @param data Start of the in-memory file buffer.
 * @param size Total buffer size in bytes.
 * @param expected_magic Expected magic value (e.g. `kXtgMagic` or `kXthMagic`).
 * @param out Output struct to populate on success.
 * @param payload Output pointer to the start of the payload (immediately after the header).
 * @return `true` on success, `false` if the header is invalid or truncated.
 */
static inline bool parseXtxHeader(
    const uint8_t* data,
    size_t size,
    uint32_t expected_magic,
    XtxImageHeader* out,
    const uint8_t** payload) {
    if (!data || !out || !payload) {
        return false;
    }
    if (size < kXtxHeaderSize) {
        return false;
    }

    const uint32_t magic = loadLeU32(data + 0x00);
    if (magic != expected_magic) {
        return false;
    }

    out->width = loadLeU16(data + 0x04);
    out->height = loadLeU16(data + 0x06);
    out->color_mode = data[0x08];
    out->compression = data[0x09];
    out->data_size = loadLeU32(data + 0x0A);
    memcpy(out->md5_8, data + 0x0E, sizeof(out->md5_8));

    *payload = data + kXtxHeaderSize;
    return true;
}

/**
 * @brief Parse an XTG header.
 * @param data Start of the in-memory file buffer.
 * @param size Total buffer size in bytes.
 * @param out Output struct to populate on success.
 * @param payload Output pointer to the start of the payload.
 * @return `true` on success.
 */
static inline bool parseXtgHeader(const uint8_t* data, size_t size, XtxImageHeader* out, const uint8_t** payload) {
    return parseXtxHeader(data, size, kXtgMagic, out, payload);
}

/**
 * @brief Parse an XTH header.
 * @param data Start of the in-memory file buffer.
 * @param size Total buffer size in bytes.
 * @param out Output struct to populate on success.
 * @param payload Output pointer to the start of the payload.
 * @return `true` on success.
 */
static inline bool parseXthHeader(const uint8_t* data, size_t size, XtxImageHeader* out, const uint8_t** payload) {
    return parseXtxHeader(data, size, kXthMagic, out, payload);
}

/**
 * @brief Reverse bit order within a byte.
 * @param b Byte to reverse.
 * @return Reversed byte (bit 7 becomes bit 0, etc.).
 */
static inline uint8_t reverse8(uint8_t b) {
    b = static_cast<uint8_t>((b >> 4) | (b << 4));
    b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

/**
 * @brief Transpose an 8x8 bit matrix packed into a 64-bit value.
 *
 * The matrix is stored as 8 bytes (rows) packed into a `uint64_t`. Bit order
 * within each byte is LSB-first on both input and output.
 *
 * @param x Packed 8x8 input matrix.
 * @return Packed 8x8 transpose.
 */
static inline uint64_t transpose8x8Lsb(uint64_t x) {
    uint64_t t;
    t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAULL;
    x ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCULL;
    x ^= t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ULL;
    x ^= t ^ (t << 28);
    return x;
}

/**
 * @brief Fill a FastEPD native 1bpp buffer region with white (0xFF).
 * @param dst Destination buffer pointer.
 * @param dst_pitch Bytes per row/column (native pitch for current rotation).
 * @param dst_native_h Native buffer height in rows/columns matching @p dst_pitch.
 */
static inline void clearNativeWhite1bpp(uint8_t* dst, int dst_pitch, int dst_native_h) {
    const size_t bytes = static_cast<size_t>(dst_pitch) * static_cast<size_t>(dst_native_h);
    memset(dst, 0xFF, bytes);
}

/**
 * @brief Fill a FastEPD native 2bpp buffer region with white (0xFF).
 * @param dst Destination buffer pointer.
 * @param dst_pitch Bytes per row/column (native pitch for current rotation).
 * @param dst_native_h Native buffer height in rows/columns matching @p dst_pitch.
 */
static inline void clearNativeWhite2bpp(uint8_t* dst, int dst_pitch, int dst_native_h) {
    const size_t bytes = static_cast<size_t>(dst_pitch) * static_cast<size_t>(dst_native_h);
    memset(dst, 0xFF, bytes);
}

/**
 * @brief Mask selecting valid pixels inside a packed 2bpp byte.
 *
 * The returned mask has 1s for pixels that should be preserved/written and 0s
 * for pixels that should be treated as padding.
 *
 * @param valid Number of valid pixels in [0..4] within the byte.
 * @return Bit mask over the packed 2bpp byte.
 */
static constexpr uint8_t xthMask2bppBytesForValidPixels(int valid) {
    // valid in [0..4], for a 2bpp byte laid out as:
    //   pix0: bits 7..6, pix1: 5..4, pix2: 3..2, pix3: 1..0
    switch (valid) {
        case 0:
            return 0x00;
        case 1:
            return 0xC0;
        case 2:
            return 0xF0;
        case 3:
            return 0xFC;
        default:
            return 0xFF;
    }
}

/**
 * @brief Convert two 4-pixel XTH bitplane nibbles into one FastEPD packed 2bpp byte.
 *
 * XTH pixel values are stored as two 1bpp planes and interpreted as a 2-bit value
 * (0..3). FastEPD uses the inverse polarity for grayscale indices; this LUT entry
 * performs the conversion.
 *
 * @param nib1 High bitplane nibble (4 pixels, top-to-bottom).
 * @param nib2 Low bitplane nibble (4 pixels, top-to-bottom).
 * @return Packed 2bpp byte holding 4 converted pixels.
 */
static constexpr uint8_t xthLut4Entry(uint8_t nib1, uint8_t nib2) {
    uint8_t out = 0;
    // nib1/nib2 are 4 pixels, top-to-bottom:
    //   bit3 => pixel0, bit2 => pixel1, bit1 => pixel2, bit0 => pixel3
    for (int i = 0; i < 4; i++) {
        const uint8_t b1 = (nib1 >> (3 - i)) & 1u;
        const uint8_t b2 = (nib2 >> (3 - i)) & 1u;
        const uint8_t xth_val = static_cast<uint8_t>((b1 << 1) | b2); // 0..3, 0==white, 3==black
        const uint8_t epd_val = static_cast<uint8_t>(3u - xth_val); // FastEPD: 0==black, 3==white
        out |= static_cast<uint8_t>(epd_val << ((3 - i) * 2));
    }
    return out;
}

/**
 * @brief Build the full 256-entry nibble-pair LUT for XTH -> FastEPD conversion.
 * @return Lookup table indexed by `(nib1 << 4) | nib2`.
 */
static constexpr std::array<uint8_t, 256> makeXthLut4() {
    std::array<uint8_t, 256> lut = {};
    for (int a = 0; a < 16; a++) {
        for (int b = 0; b < 16; b++) {
            lut[static_cast<size_t>((a << 4) | b)] = xthLut4Entry(static_cast<uint8_t>(a), static_cast<uint8_t>(b));
        }
    }
    return lut;
}

/**
 * @brief LUT mapping two XTH plane nibbles to a packed 2bpp output byte.
 *
 * Index is `((plane1_nibble << 4) | plane2_nibble)`.
 */
static constexpr auto kXthLut4 = makeXthLut4();
static_assert(kXthLut4[0x00] == 0xFF, "XTH LUT: 00/00 should map to white (0xFF)");
static_assert(kXthLut4[0xFF] == 0x00, "XTH LUT: FF/FF should map to black (0x00)");
static_assert(kXthLut4[0xF0] == 0x55, "XTH LUT: 1/0 bits should map to 0x55");

/**
 * @brief Reverse the order of four packed 2bpp pixels within a byte.
 * @param b Packed 2bpp byte containing pixels `[p0 p1 p2 p3]`.
 * @return Packed 2bpp byte containing pixels `[p3 p2 p1 p0]`.
 */
static inline uint8_t reverse4Pix2bpp(uint8_t b) {
    // Reverse order of four 2-bit pixels within a byte:
    // [p0 p1 p2 p3] -> [p3 p2 p1 p0]
    return static_cast<uint8_t>(((b & 0x03u) << 6) | ((b & 0x0Cu) << 2) | ((b & 0x30u) >> 2) | ((b & 0xC0u) >> 6));
}

/**
 * @brief Compute the bit mask to select valid rows from an XTH source byte.
 *
 * XTH columns are packed with the top pixel in the MSB of each byte. This mask keeps
 * only the valid rows for a given 8-row block and clears out-of-image rows (treated as white).
 *
 * @param block_h Number of rows to be written for this block (1..8).
 * @param src_tail_rows Number of valid rows in the last source byte (0 means 8).
 * @return Mask with 1s for valid rows in MSB-first order.
 */
static inline uint8_t xthSrcMaskForYBlock(int block_h, int src_tail_rows) {
    // Mask to keep the valid top bits for this 8-row group, clearing rows beyond image (treated as white).
    int valid = block_h;
    if (src_tail_rows != 0 && src_tail_rows < valid) {
        valid = src_tail_rows;
    }
    if (valid >= 8) {
        return 0xFF;
    }
    if (valid <= 0) {
        return 0x00;
    }
    return static_cast<uint8_t>(0xFFu << (8 - valid));
}

/**
 * @brief Pack eight column bytes into a `uint64_t` for SWAR transpose operations.
 *
 * Each input byte is bit-reversed so that the returned `uint64_t` is LSB-first within
 * each byte, which matches `transpose8x8Lsb()`.
 *
 * @param b Array of 8 column bytes.
 * @return Packed `uint64_t` value.
 */
static inline uint64_t xthPack8ColsLsb(const uint8_t b[8]) {
    return static_cast<uint64_t>(reverse8(b[0])) | (static_cast<uint64_t>(reverse8(b[1])) << 8) |
        (static_cast<uint64_t>(reverse8(b[2])) << 16) | (static_cast<uint64_t>(reverse8(b[3])) << 24) |
        (static_cast<uint64_t>(reverse8(b[4])) << 32) | (static_cast<uint64_t>(reverse8(b[5])) << 40) |
        (static_cast<uint64_t>(reverse8(b[6])) << 48) | (static_cast<uint64_t>(reverse8(b[7])) << 56);
}

/**
 * @brief Blit an XTH (2-plane) image into a FastEPD native 2bpp buffer (rotation=0 layout).
 *
 * Writes the top-left `copy_w` x `copy_h` region. Out-of-image pixels are treated as white.
 *
 * @param dst Destination native 2bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (native width / 4).
 * @param src_plane1 First XTH bitplane (column-major, 8 rows per byte).
 * @param src_plane2 Second XTH bitplane (column-major, 8 rows per byte).
 * @param src_w Source width in pixels.
 * @param src_h Source height in pixels.
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xthBlitRot0TopLeftClipped2bpp(
    uint8_t* dst,
    int dst_pitch,
    const uint8_t* src_plane1,
    const uint8_t* src_plane2,
    int src_w,
    int src_h,
    int copy_w,
    int copy_h) {
    // Convert XTH (vertical scan order bitplanes) into FastEPD 2bpp row-major buffer (rotation=0 layout).
    const int src_col_bytes = (src_h + 7) >> 3;
    const int src_tail_rows = src_h & 7;

    for (int y0 = 0; y0 < copy_h; y0 += 8) {
        const int block_h = (copy_h - y0 >= 8) ? 8 : (copy_h - y0);
        const int y_byte = y0 >> 3;
        const int remaining_rows_in_src_byte =
            (y_byte == (src_col_bytes - 1)) ? ((src_tail_rows == 0) ? 8 : src_tail_rows) : 8;
        const uint8_t y_mask = xthSrcMaskForYBlock(block_h, remaining_rows_in_src_byte);

        for (int x0 = 0; x0 < copy_w; x0 += 8) {
            const int block_w = (copy_w - x0 >= 8) ? 8 : (copy_w - x0);

            uint8_t c1[8];
            uint8_t c2[8];
            for (int c = 0; c < 8; c++) {
                if (c < block_w) {
                    const int sx = x0 + c;
                    const int src_col = src_w - 1 - sx;
                    const size_t idx = static_cast<size_t>(src_col) * static_cast<size_t>(src_col_bytes) +
                        static_cast<size_t>(y_byte);
                    c1[c] = static_cast<uint8_t>(src_plane1[idx] & y_mask);
                    c2[c] = static_cast<uint8_t>(src_plane2[idx] & y_mask);
                } else {
                    c1[c] = 0x00;
                    c2[c] = 0x00;
                }
            }

            uint64_t p1 = transpose8x8Lsb(xthPack8ColsLsb(c1));
            uint64_t p2 = transpose8x8Lsb(xthPack8ColsLsb(c2));

            const int dst_byte = x0 >> 2;
            for (int r = 0; r < block_h; r++) {
                const uint8_t row1 = reverse8(static_cast<uint8_t>((p1 >> (r * 8)) & 0xFF));
                const uint8_t row2 = reverse8(static_cast<uint8_t>((p2 >> (r * 8)) & 0xFF));

                const uint8_t out0 = kXthLut4[static_cast<size_t>(((row1 >> 4) << 4) | (row2 >> 4))];
                const uint8_t out1 = kXthLut4[static_cast<size_t>(((row1 & 0x0F) << 4) | (row2 & 0x0F))];

                uint8_t* d = dst + static_cast<size_t>(y0 + r) * static_cast<size_t>(dst_pitch) +
                    static_cast<size_t>(dst_byte);

                if (block_w >= 8) {
                    d[0] = out0;
                    d[1] = out1;
                } else if (block_w > 4) {
                    d[0] = out0;
                    const uint8_t mask = xthMask2bppBytesForValidPixels(block_w - 4);
                    d[1] = static_cast<uint8_t>((out1 & mask) | static_cast<uint8_t>(~mask));
                } else {
                    const uint8_t mask = xthMask2bppBytesForValidPixels(block_w);
                    d[0] = static_cast<uint8_t>((out0 & mask) | static_cast<uint8_t>(~mask));
                }
            }
        }
    }
}

/**
 * @brief Blit an XTH (2-plane) image into a FastEPD native 2bpp buffer (rotation=90 layout).
 *
 * Writes the top-left `copy_w` x `copy_h` region. Out-of-image pixels are treated as white.
 *
 * @param dst Destination native 2bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (native width / 4).
 * @param dst_logical_w Logical width (used to map columns for rotation).
 * @param src_plane1 First XTH bitplane (column-major, 8 rows per byte).
 * @param src_plane2 Second XTH bitplane (column-major, 8 rows per byte).
 * @param src_w Source width in pixels.
 * @param src_h Source height in pixels.
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xthBlitRot90TopLeftClipped2bpp(
    uint8_t* dst,
    int dst_pitch,
    int dst_logical_w,
    const uint8_t* src_plane1,
    const uint8_t* src_plane2,
    int src_w,
    int src_h,
    int copy_w,
    int copy_h) {
    // XTH is stored in the same vertical-scan order used by FastEPD's rotation=90 native buffer:
    // columns right-to-left, 8 vertical pixels per byte, MSB = top pixel in group.
    //
    // FastEPD's 2bpp buffer at rotation=90 is also column-major right-to-left, but packs 4 vertical pixels per byte.
    // We combine the 2 bitplanes into packed 2bpp bytes.
    const int src_col_bytes = (src_h + 7) >> 3; // bytes per column per plane
    const int src_tail_rows = src_h & 7;
    const int dst_col_bytes = dst_pitch; // bytes per column in packed 2bpp

    const int full8 = copy_h & ~7;
    const int tail = copy_h - full8;

    for (int x = 0; x < copy_w; x++) {
        const int src_col = src_w - 1 - x;
        const int dst_col = dst_logical_w - 1 - x;
        const uint8_t* s1 = src_plane1 + static_cast<size_t>(src_col) * static_cast<size_t>(src_col_bytes);
        const uint8_t* s2 = src_plane2 + static_cast<size_t>(src_col) * static_cast<size_t>(src_col_bytes);
        uint8_t* dcol = dst + static_cast<size_t>(dst_col) * static_cast<size_t>(dst_pitch);

        // Full 8-pixel blocks -> 2 packed bytes each.
        for (int y = 0; y < full8; y += 8) {
            const int si = y >> 3;
            uint8_t b1 = s1[si];
            uint8_t b2 = s2[si];
            if (src_tail_rows != 0 && si == (src_col_bytes - 1)) {
                const uint8_t m = static_cast<uint8_t>(0xFFu << (8 - src_tail_rows));
                b1 &= m;
                b2 &= m;
            }

            const uint8_t out0 = kXthLut4[static_cast<size_t>(((b1 >> 4) << 4) | (b2 >> 4))];
            const uint8_t out1 = kXthLut4[static_cast<size_t>(((b1 & 0x0F) << 4) | (b2 & 0x0F))];

            uint8_t* d = dcol + (y >> 2);
            d[0] = out0;
            d[1] = out1;
        }

        if (tail == 0) {
            continue;
        }

        // Tail (1..7 pixels) in the last 8-pixel block.
        const int y = full8;
        const int si = y >> 3;
        uint8_t b1 = (si < src_col_bytes) ? s1[si] : 0x00; // 0 bits => XTH value 0 => white
        uint8_t b2 = (si < src_col_bytes) ? s2[si] : 0x00;
        if (src_tail_rows != 0 && si == (src_col_bytes - 1)) {
            const uint8_t m = static_cast<uint8_t>(0xFFu << (8 - src_tail_rows));
            b1 &= m;
            b2 &= m;
        }

        const uint8_t out0 = kXthLut4[static_cast<size_t>(((b1 >> 4) << 4) | (b2 >> 4))];
        const uint8_t out1 = kXthLut4[static_cast<size_t>(((b1 & 0x0F) << 4) | (b2 & 0x0F))];

        const int out_i = y >> 2;
        if (out_i < dst_col_bytes) {
            const int valid0 = (tail >= 4) ? 4 : tail;
            const uint8_t mask0 = xthMask2bppBytesForValidPixels(valid0);
            uint8_t* d0 = dcol + out_i;
            *d0 = static_cast<uint8_t>((out0 & mask0) | static_cast<uint8_t>(~mask0));
        }
        if (tail > 4 && (out_i + 1) < dst_col_bytes) {
            const int valid1 = tail - 4;
            const uint8_t mask1 = xthMask2bppBytesForValidPixels(valid1);
            uint8_t* d1 = dcol + (out_i + 1);
            *d1 = static_cast<uint8_t>((out1 & mask1) | static_cast<uint8_t>(~mask1));
        }
    }
}

/**
 * @brief Blit an XTH (2-plane) image into a FastEPD native 2bpp buffer (rotation=180 layout).
 *
 * Writes the top-left `copy_w` x `copy_h` region in logical coordinates.
 * Out-of-image pixels are treated as white.
 *
 * @param dst Destination native 2bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (native width / 4).
 * @param dst_w Destination logical width in pixels.
 * @param dst_h Destination logical height in pixels.
 * @param src_plane1 First XTH bitplane (column-major, 8 rows per byte).
 * @param src_plane2 Second XTH bitplane (column-major, 8 rows per byte).
 * @param src_w Source width in pixels.
 * @param src_h Source height in pixels.
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xthBlitRot180TopLeftClipped2bpp(
    uint8_t* dst,
    int dst_pitch,
    int dst_w,
    int dst_h,
    const uint8_t* src_plane1,
    const uint8_t* src_plane2,
    int src_w,
    int src_h,
    int copy_w,
    int copy_h) {
    // Convert XTH into FastEPD 2bpp buffer (rotation=180 layout).
    // Implemented as 8x8 bitplane transposes + horizontal/vertical reversal into native memory.
    const int src_col_bytes = (src_h + 7) >> 3;
    const int src_tail_rows = src_h & 7;

    for (int y0 = 0; y0 < copy_h; y0 += 8) {
        const int block_h = (copy_h - y0 >= 8) ? 8 : (copy_h - y0);
        const int y_byte = y0 >> 3;
        const int remaining_rows_in_src_byte =
            (y_byte == (src_col_bytes - 1)) ? ((src_tail_rows == 0) ? 8 : src_tail_rows) : 8;
        const uint8_t y_mask = xthSrcMaskForYBlock(block_h, remaining_rows_in_src_byte);

        for (int x0 = 0; x0 < copy_w; x0 += 8) {
            const int block_w = (copy_w - x0 >= 8) ? 8 : (copy_w - x0);

            uint8_t c1[8];
            uint8_t c2[8];
            for (int c = 0; c < 8; c++) {
                if (c < block_w) {
                    const int sx = x0 + c;
                    const int src_col = src_w - 1 - sx;
                    const size_t idx = static_cast<size_t>(src_col) * static_cast<size_t>(src_col_bytes) +
                        static_cast<size_t>(y_byte);
                    c1[c] = static_cast<uint8_t>(src_plane1[idx] & y_mask);
                    c2[c] = static_cast<uint8_t>(src_plane2[idx] & y_mask);
                } else {
                    c1[c] = 0x00;
                    c2[c] = 0x00;
                }
            }

            uint64_t p1 = transpose8x8Lsb(xthPack8ColsLsb(c1));
            uint64_t p2 = transpose8x8Lsb(xthPack8ColsLsb(c2));

            const int dest_x_full = dst_w - x0 - 8;
            const int clip_left_px = (dest_x_full < 0) ? -dest_x_full : 0;
            const int clip_right_px = (dest_x_full + 8 > dst_w) ? (dest_x_full + 8 - dst_w) : 0;
            const int write_px = 8 - clip_left_px - clip_right_px;
            if (write_px <= 0) {
                continue;
            }
            const int dest_x = dest_x_full + clip_left_px;
            const int dest_byte = dest_x >> 2;

            for (int r = 0; r < block_h; r++) {
                const uint8_t row1 = reverse8(static_cast<uint8_t>((p1 >> (r * 8)) & 0xFF));
                const uint8_t row2 = reverse8(static_cast<uint8_t>((p2 >> (r * 8)) & 0xFF));

                const uint8_t out0 = kXthLut4[static_cast<size_t>(((row1 >> 4) << 4) | (row2 >> 4))];
                const uint8_t out1 = kXthLut4[static_cast<size_t>(((row1 & 0x0F) << 4) | (row2 & 0x0F))];

                const uint8_t rev0 = reverse4Pix2bpp(out1);
                const uint8_t rev1 = reverse4Pix2bpp(out0);
                uint16_t u = static_cast<uint16_t>((static_cast<uint16_t>(rev0) << 8) | rev1);
                if (clip_left_px != 0) {
                    u = static_cast<uint16_t>(u << (clip_left_px * 2));
                }

                const int dy = dst_h - 1 - (y0 + r);
                uint8_t* d = dst + static_cast<size_t>(dy) * static_cast<size_t>(dst_pitch) +
                    static_cast<size_t>(dest_byte);

                const uint8_t b0 = static_cast<uint8_t>(u >> 8);
                const uint8_t b1 = static_cast<uint8_t>(u & 0xFF);

                if (write_px >= 8) {
                    d[0] = b0;
                    d[1] = b1;
                } else if (write_px > 4) {
                    d[0] = b0;
                    const uint8_t mask = xthMask2bppBytesForValidPixels(write_px - 4);
                    d[1] = static_cast<uint8_t>((b1 & mask) | static_cast<uint8_t>(~mask));
                } else {
                    const uint8_t mask = xthMask2bppBytesForValidPixels(write_px);
                    d[0] = static_cast<uint8_t>((b0 & mask) | static_cast<uint8_t>(~mask));
                }
            }
        }
    }
}

/**
 * @brief Blit an XTH (2-plane) image into a FastEPD native 2bpp buffer (rotation=270 layout).
 *
 * Writes the top-left `copy_w` x `copy_h` region. Out-of-image pixels are treated as white.
 *
 * @param dst Destination native 2bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (native width / 4).
 * @param dst_logical_h Logical height (used to map row groups for rotation).
 * @param src_plane1 First XTH bitplane (column-major, 8 rows per byte).
 * @param src_plane2 Second XTH bitplane (column-major, 8 rows per byte).
 * @param src_w Source width in pixels.
 * @param src_h Source height in pixels.
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xthBlitRot270TopLeftClipped2bpp(
    uint8_t* dst,
    int dst_pitch,
    int dst_logical_h,
    const uint8_t* src_plane1,
    const uint8_t* src_plane2,
    int src_w,
    int src_h,
    int copy_w,
    int copy_h) {
    // FastEPD rotation=270 layout is column-major left-to-right, with vertical groups reversed
    // and 4-pixel order reversed within each byte.
    const int src_col_bytes = (src_h + 7) >> 3;
    const int src_tail_rows = src_h & 7;
    const int dst_groups = dst_logical_h >> 2;

    const int full8 = copy_h & ~7;
    const int tail = copy_h - full8;

    for (int x = 0; x < copy_w; x++) {
        const int src_col = src_w - 1 - x;
        const uint8_t* s1 = src_plane1 + static_cast<size_t>(src_col) * static_cast<size_t>(src_col_bytes);
        const uint8_t* s2 = src_plane2 + static_cast<size_t>(src_col) * static_cast<size_t>(src_col_bytes);
        uint8_t* dcol = dst + static_cast<size_t>(x) * static_cast<size_t>(dst_pitch);

        for (int y = 0; y < full8; y += 8) {
            const int si = y >> 3;
            uint8_t b1 = s1[si];
            uint8_t b2 = s2[si];
            if (src_tail_rows != 0 && si == (src_col_bytes - 1)) {
                const uint8_t m = static_cast<uint8_t>(0xFFu << (8 - src_tail_rows));
                b1 &= m;
                b2 &= m;
            }

            const uint8_t out0 = kXthLut4[static_cast<size_t>(((b1 >> 4) << 4) | (b2 >> 4))];
            const uint8_t out1 = kXthLut4[static_cast<size_t>(((b1 & 0x0F) << 4) | (b2 & 0x0F))];

            const int group0 = y >> 2;
            const int dst0 = (dst_groups - 1) - group0;
            const int dst1 = dst0 - 1;
            if (dst0 >= 0 && dst0 < dst_pitch) {
                dcol[dst0] = reverse4Pix2bpp(out0);
            }
            if (dst1 >= 0 && dst1 < dst_pitch) {
                dcol[dst1] = reverse4Pix2bpp(out1);
            }
        }

        if (tail == 0) {
            continue;
        }

        const int y = full8;
        const int si = y >> 3;
        uint8_t b1 = (si < src_col_bytes) ? s1[si] : 0x00;
        uint8_t b2 = (si < src_col_bytes) ? s2[si] : 0x00;
        if (src_tail_rows != 0 && si == (src_col_bytes - 1)) {
            const uint8_t m = static_cast<uint8_t>(0xFFu << (8 - src_tail_rows));
            b1 &= m;
            b2 &= m;
        }

        const uint8_t out0 = kXthLut4[static_cast<size_t>(((b1 >> 4) << 4) | (b2 >> 4))];
        const uint8_t out1 = kXthLut4[static_cast<size_t>(((b1 & 0x0F) << 4) | (b2 & 0x0F))];

        const int group0 = y >> 2;
        const int dst0 = (dst_groups - 1) - group0;
        const int dst1 = dst0 - 1;
        if (dst0 >= 0 && dst0 < dst_pitch) {
            const int valid0 = (tail >= 4) ? 4 : tail;
            const uint8_t mask0 = xthMask2bppBytesForValidPixels(valid0);
            const uint8_t filled0 = static_cast<uint8_t>((out0 & mask0) | static_cast<uint8_t>(~mask0));
            dcol[dst0] = reverse4Pix2bpp(filled0);
        }
        if (tail > 4 && dst1 >= 0 && dst1 < dst_pitch) {
            const int valid1 = tail - 4;
            const uint8_t mask1 = xthMask2bppBytesForValidPixels(valid1);
            const uint8_t filled1 = static_cast<uint8_t>((out1 & mask1) | static_cast<uint8_t>(~mask1));
            dcol[dst1] = reverse4Pix2bpp(filled1);
        }
    }
}

/**
 * @brief Fast-path blit for an exact-size XTG (1bpp) image at rotation=0.
 *
 * Copies the bitmap row-major and ensures any padding bits beyond the width are set to white.
 *
 * @param dst Destination native 1bpp buffer pointer.
 * @param src Source packed 1bpp bitmap (MSB-first within each byte).
 * @param w Width in pixels.
 * @param h Height in pixels.
 */
static inline void xtgBlitRot0Fullscreen1bpp(uint8_t* dst, const uint8_t* src, int w, int h) {
    const int pitch = (w + 7) >> 3;
    const size_t bytes = static_cast<size_t>(pitch) * static_cast<size_t>(h);
    memcpy(dst, src, bytes);

    const int r = w & 7;
    if (r == 0) {
        return;
    }
    const uint8_t pad_mask = static_cast<uint8_t>((1u << (8 - r)) - 1u); // low bits are unused
    uint8_t* row = dst + (pitch - 1);
    for (int y = 0; y < h; y++) {
        row[static_cast<size_t>(y) * static_cast<size_t>(pitch)] |= pad_mask;
    }
}

/**
 * @brief Blit an XTG (1bpp) bitmap into a FastEPD native 1bpp buffer (rotation=0 layout).
 *
 * Copies only the top-left `copy_w` x `copy_h` region, preserving destination padding bits.
 *
 * @param dst Destination native 1bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (`(dst_w + 7) / 8`).
 * @param src Source packed 1bpp bitmap (MSB-first within each byte).
 * @param src_pitch Source pitch in bytes (`(src_w + 7) / 8`).
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xtgBlitRot0TopLeftClipped1bpp(
    uint8_t* dst,
    int dst_pitch,
    const uint8_t* src,
    int src_pitch,
    int copy_w,
    int copy_h) {
    const int full_bytes = copy_w >> 3;
    const int tail_bits = copy_w & 7;
    const uint8_t tail_mask_used =
        (tail_bits == 0) ? 0 : static_cast<uint8_t>(0xFFu << (8 - static_cast<uint8_t>(tail_bits)));

    for (int y = 0; y < copy_h; y++) {
        uint8_t* d = dst + static_cast<size_t>(y) * static_cast<size_t>(dst_pitch);
        const uint8_t* s = src + static_cast<size_t>(y) * static_cast<size_t>(src_pitch);

        if (full_bytes > 0) {
            memcpy(d, s, static_cast<size_t>(full_bytes));
        }
        if (tail_bits != 0) {
            const uint8_t sb = s[full_bytes];
            uint8_t db = d[full_bytes];
            db = static_cast<uint8_t>((db & ~tail_mask_used) | (sb & tail_mask_used));
            d[full_bytes] = db;
        }
    }
}

/**
 * @brief Blit an XTG (1bpp) bitmap into a FastEPD native 1bpp buffer (rotation=90 layout).
 *
 * Copies the top-left `copy_w` x `copy_h` region by transposing 8x8 blocks.
 *
 * @param dst Destination native 1bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (native pitch for rotation=90).
 * @param dst_logical_w Destination logical width in pixels (used for mapping).
 * @param src Source packed 1bpp bitmap (MSB-first within each byte).
 * @param src_pitch Source pitch in bytes (`(src_w + 7) / 8`).
 * @param src_w Source width in pixels (for source padding).
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xtgBlitRot90TopLeftClipped1bpp(
    uint8_t* dst,
    int dst_pitch,
    int dst_logical_w,
    const uint8_t* src,
    int src_pitch,
    int src_w,
    int copy_w,
    int copy_h) {
    // Logical (w,h) -> native (h,w), using the same mapping as bbepSetPixelFast2Clr_90.
    // For each 8x8 block in the source image, transpose bits and write 8 destination bytes.
    const int max_sx_bytes = (copy_w + 7) >> 3;
    const int src_tail_bits = src_w & 7;
    const uint8_t src_pad_mask = (src_tail_bits == 0) ? 0 : static_cast<uint8_t>((1u << (8 - src_tail_bits)) - 1u);
    for (int sy = 0; sy < copy_h; sy += 8) {
        const int dst_x_byte = sy >> 3;
        const uint8_t* s_base = src + static_cast<size_t>(sy) * static_cast<size_t>(src_pitch);
        for (int sx_byte = 0; sx_byte < max_sx_bytes; sx_byte++) {
            const int base_x = sx_byte << 3;
            int valid_cols = copy_w - base_x;
            if (valid_cols <= 0) {
                break;
            }
            if (valid_cols > 8) {
                valid_cols = 8;
            }

            uint8_t b0 = (sy + 0 < copy_h) ? s_base[sx_byte + 0 * src_pitch] : 0xFF;
            uint8_t b1 = (sy + 1 < copy_h) ? s_base[sx_byte + 1 * src_pitch] : 0xFF;
            uint8_t b2 = (sy + 2 < copy_h) ? s_base[sx_byte + 2 * src_pitch] : 0xFF;
            uint8_t b3 = (sy + 3 < copy_h) ? s_base[sx_byte + 3 * src_pitch] : 0xFF;
            uint8_t b4 = (sy + 4 < copy_h) ? s_base[sx_byte + 4 * src_pitch] : 0xFF;
            uint8_t b5 = (sy + 5 < copy_h) ? s_base[sx_byte + 5 * src_pitch] : 0xFF;
            uint8_t b6 = (sy + 6 < copy_h) ? s_base[sx_byte + 6 * src_pitch] : 0xFF;
            uint8_t b7 = (sy + 7 < copy_h) ? s_base[sx_byte + 7 * src_pitch] : 0xFF;
            if (src_pad_mask != 0 && sx_byte == (src_pitch - 1)) {
                b0 |= src_pad_mask;
                b1 |= src_pad_mask;
                b2 |= src_pad_mask;
                b3 |= src_pad_mask;
                b4 |= src_pad_mask;
                b5 |= src_pad_mask;
                b6 |= src_pad_mask;
                b7 |= src_pad_mask;
            }

            // Convert MSB-first -> LSB-first for a SWAR transpose.
            uint64_t x = static_cast<uint64_t>(reverse8(b0)) | (static_cast<uint64_t>(reverse8(b1)) << 8) |
                (static_cast<uint64_t>(reverse8(b2)) << 16) | (static_cast<uint64_t>(reverse8(b3)) << 24) |
                (static_cast<uint64_t>(reverse8(b4)) << 32) | (static_cast<uint64_t>(reverse8(b5)) << 40) |
                (static_cast<uint64_t>(reverse8(b6)) << 48) | (static_cast<uint64_t>(reverse8(b7)) << 56);

            x = transpose8x8Lsb(x);

            // Destination Y is reversed across X: dy = (w-1) - x.
            uint8_t* d = dst + static_cast<size_t>(dst_logical_w - 1 - base_x) * static_cast<size_t>(dst_pitch) +
                static_cast<size_t>(dst_x_byte);
            for (int c = 0; c < valid_cols; c++) {
                const uint8_t out_lsb = static_cast<uint8_t>((x >> (c * 8)) & 0xFF);
                *d = reverse8(out_lsb); // LSB-first -> MSB-first
                d -= dst_pitch;
            }
        }
    }
}

/**
 * @brief Blit an XTG (1bpp) bitmap into a FastEPD native 1bpp buffer (rotation=180 layout).
 *
 * Copies the top-left `copy_w` x `copy_h` region while applying both horizontal and vertical flips.
 *
 * @param dst Destination native 1bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (`(dst_w + 7) / 8`).
 * @param dst_w Destination logical width in pixels.
 * @param dst_h Destination logical height in pixels.
 * @param src Source packed 1bpp bitmap (MSB-first within each byte).
 * @param src_pitch Source pitch in bytes (`(src_w + 7) / 8`).
 * @param src_w Source width in pixels (for source padding).
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xtgBlitRot180TopLeftClipped1bpp(
    uint8_t* dst,
    int dst_pitch,
    int dst_w,
    int dst_h,
    const uint8_t* src,
    int src_pitch,
    int src_w,
    int copy_w,
    int copy_h) {
    // Logical (w,h) -> native (w,h), using the same mapping as bbepSetPixelFast2Clr_180.
    const int max_sx_bytes = (copy_w + 7) >> 3;
    const int src_tail_bits = src_w & 7;
    const uint8_t src_pad_mask = (src_tail_bits == 0) ? 0 : static_cast<uint8_t>((1u << (8 - src_tail_bits)) - 1u);

    const uint8_t* s_base = src;
    for (int sy = 0; sy < copy_h; sy += 8) {
        for (int sx_byte = 0; sx_byte < max_sx_bytes; sx_byte++) {
            const int base_x = sx_byte << 3;
            int valid_cols = copy_w - base_x;
            if (valid_cols <= 0) {
                break;
            }
            if (valid_cols > 8) {
                valid_cols = 8;
            }

            uint8_t b[8];
            for (int r = 0; r < 8; r++) {
                const int src_y = sy + r;
                b[r] = (src_y < copy_h) ? s_base[static_cast<size_t>(src_y) * static_cast<size_t>(src_pitch) + sx_byte] : 0xFF;
            }
            if (src_pad_mask != 0 && sx_byte == (src_pitch - 1)) {
                for (int r = 0; r < 8; r++) {
                    b[r] |= src_pad_mask;
                }
            }

            // Horizontal flip is reverse bits; vertical flip is reversing the row order.
            const uint8_t mask = (valid_cols == 8) ? 0xFF : static_cast<uint8_t>((1u << valid_cols) - 1u);
            const int dst_x_byte = ((dst_w - 1 - base_x) >> 3); // byte address for the right edge of this block
            for (int r = 0; r < 8; r++) {
                const int dst_y = dst_h - 1 - (sy + r);
                if (dst_y < 0) {
                    break;
                }
                uint8_t out = reverse8(b[r]);
                if (mask != 0xFF) {
                    uint8_t* d = dst + static_cast<size_t>(dst_y) * static_cast<size_t>(dst_pitch) + dst_x_byte;
                    const uint8_t prev = *d;
                    *d = static_cast<uint8_t>((prev & ~mask) | (out & mask));
                } else {
                    dst[static_cast<size_t>(dst_y) * static_cast<size_t>(dst_pitch) + dst_x_byte] = out;
                }
            }
        }
    }
}

/**
 * @brief Blit an XTG (1bpp) bitmap into a FastEPD native 1bpp buffer (rotation=270 layout).
 *
 * Copies the top-left `copy_w` x `copy_h` region by transposing 8x8 blocks and mapping into
 * the rotation=270 native layout.
 *
 * @param dst Destination native 1bpp buffer pointer.
 * @param dst_pitch Destination pitch in bytes (native pitch for rotation=270).
 * @param dst_logical_h Destination logical height in pixels (used for mapping).
 * @param src Source packed 1bpp bitmap (MSB-first within each byte).
 * @param src_pitch Source pitch in bytes (`(src_w + 7) / 8`).
 * @param src_w Source width in pixels (for source padding).
 * @param copy_w Number of pixels to copy horizontally.
 * @param copy_h Number of pixels to copy vertically.
 */
static inline void xtgBlitRot270TopLeftClipped1bpp(
    uint8_t* dst,
    int dst_pitch,
    int dst_logical_h,
    const uint8_t* src,
    int src_pitch,
    int src_w,
    int copy_w,
    int copy_h) {
    // Logical (w,h) -> native (h,w), using the same mapping as bbepSetPixelFast2Clr_270.
    const int max_sx_bytes = (copy_w + 7) >> 3;
    const int src_tail_bits = src_w & 7;
    const uint8_t src_pad_mask = (src_tail_bits == 0) ? 0 : static_cast<uint8_t>((1u << (8 - src_tail_bits)) - 1u);

    for (int sy = 0; sy < copy_h; sy += 8) {
        const int dst_x_byte = (dst_logical_h - 1 - sy) >> 3;
        const uint8_t* s_base = src + static_cast<size_t>(sy) * static_cast<size_t>(src_pitch);
        for (int sx_byte = 0; sx_byte < max_sx_bytes; sx_byte++) {
            const int base_x = sx_byte << 3;
            int valid_cols = copy_w - base_x;
            if (valid_cols <= 0) {
                break;
            }
            if (valid_cols > 8) {
                valid_cols = 8;
            }

            uint8_t b0 = (sy + 0 < copy_h) ? s_base[sx_byte + 0 * src_pitch] : 0xFF;
            uint8_t b1 = (sy + 1 < copy_h) ? s_base[sx_byte + 1 * src_pitch] : 0xFF;
            uint8_t b2 = (sy + 2 < copy_h) ? s_base[sx_byte + 2 * src_pitch] : 0xFF;
            uint8_t b3 = (sy + 3 < copy_h) ? s_base[sx_byte + 3 * src_pitch] : 0xFF;
            uint8_t b4 = (sy + 4 < copy_h) ? s_base[sx_byte + 4 * src_pitch] : 0xFF;
            uint8_t b5 = (sy + 5 < copy_h) ? s_base[sx_byte + 5 * src_pitch] : 0xFF;
            uint8_t b6 = (sy + 6 < copy_h) ? s_base[sx_byte + 6 * src_pitch] : 0xFF;
            uint8_t b7 = (sy + 7 < copy_h) ? s_base[sx_byte + 7 * src_pitch] : 0xFF;
            if (src_pad_mask != 0 && sx_byte == (src_pitch - 1)) {
                b0 |= src_pad_mask;
                b1 |= src_pad_mask;
                b2 |= src_pad_mask;
                b3 |= src_pad_mask;
                b4 |= src_pad_mask;
                b5 |= src_pad_mask;
                b6 |= src_pad_mask;
                b7 |= src_pad_mask;
            }

            // Convert MSB-first -> LSB-first for a SWAR transpose.
            uint64_t x = static_cast<uint64_t>(reverse8(b0)) | (static_cast<uint64_t>(reverse8(b1)) << 8) |
                (static_cast<uint64_t>(reverse8(b2)) << 16) | (static_cast<uint64_t>(reverse8(b3)) << 24) |
                (static_cast<uint64_t>(reverse8(b4)) << 32) | (static_cast<uint64_t>(reverse8(b5)) << 40) |
                (static_cast<uint64_t>(reverse8(b6)) << 48) | (static_cast<uint64_t>(reverse8(b7)) << 56);

            x = transpose8x8Lsb(x);

            uint8_t* d = dst + static_cast<size_t>(base_x) * static_cast<size_t>(dst_pitch) + static_cast<size_t>(dst_x_byte);
            for (int c = 0; c < valid_cols; c++) {
                const uint8_t out_lsb = static_cast<uint8_t>((x >> (c * 8)) & 0xFF);
                // No final reverse8: bbepSetPixelFast2Clr_270 stores bits as (1 << (y&7)).
                *d = out_lsb;
                d += dst_pitch;
            }
        }
    }
}

}
