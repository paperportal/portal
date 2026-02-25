/**
 * @file fastepd_draw_xtc.h
 * @brief Draw XTG/XTH images into a FastEPD back buffer.
 *
 * These helpers take an in-memory XTG (1bpp) or XTH (2bpp) payload (including
 * header) and blit it into the `FASTEPD::currentBuffer()` using the current
 * rotation and mode expected by FastEPD.
 */
#pragma once

#include <FastEPD.h>

/**
 * @brief Draw an XTH (2bpp) image buffer to the EPD.
 *
 * The XTH payload is stored as two 1bpp bitplanes in a column-major,
 * 8-rows-per-byte format. This function converts it into FastEPD's native
 * 2bpp back buffer layout for the current rotation, then triggers a full update.
 *
 * @param epd FastEPD instance (must be non-null).
 * @param data Pointer to the start of the XTH file in memory (must be non-null).
 * @param size Total size of @p data in bytes.
 * @param fast Controls speed of updating the display; true means that a faster update is done.
 *
 * @note Requires `epd->getMode() == BB_MODE_2BPP`.
 * @note Supports rotations 0/90/180/270 (via `epd->getRotation()`).
 */
void draw_xth(FASTEPD* epd, const uint8_t* data, size_t size, bool fast);

/**
 * @brief Draw an XTG (1bpp) image buffer to the EPD.
 *
 * The XTG payload is a packed 1bpp bitmap (MSB-first within each byte). This
 * function blits it into FastEPD's native 1bpp back buffer layout for the
 * current rotation, then triggers a full update.
 *
 * @param epd FastEPD instance (must be non-null).
 * @param data Pointer to the start of the XTG file in memory (must be non-null).
 * @param size Total size of @p data in bytes.
 * @param fast Controls speed of updating the display; true means that a faster update is done.
 *
 * @note Requires `epd->getMode() == BB_MODE_1BPP`.
 * @note Supports rotations 0/90/180/270 (via `epd->getRotation()`).
 */
void draw_xtg(FASTEPD* epd, const uint8_t* data, size_t size, bool fast);
