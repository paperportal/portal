#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FastEPD.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "../api.h"
#include "errors.h"
#include "features.h"

extern "C" void bbepDeinitBus(void);

namespace {

constexpr const char *kTag = "wasm_api_fast_epd";

static FASTEPD g_fastept;

uint8_t *g_custom_matrix = nullptr;
size_t g_custom_matrix_size = 0;

int32_t validate_grayscale_color(int32_t color, const char *context)
{
    if (color < 0 || color > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return kWasmErrInvalidArgument;
    }
    return kWasmOk;
}

int32_t validate_optional_color(int32_t color, const char *context)
{
    if (color < -1 || color > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return kWasmErrInvalidArgument;
    }
    return kWasmOk;
}

uint8_t map_optional_color_to_fastepd(int32_t color)
{
    return (uint8_t)(color < 0 ? BBEP_TRANSPARENT : color);
}

int32_t epdInitPanel(wasm_exec_env_t exec_env, int32_t panel_type, int32_t speed)
{
    (void)exec_env;
    if (panel_type < 0 || panel_type >= BB_PANEL_COUNT) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_init_panel: panel_type out of range");
        return kWasmErrInvalidArgument;
    }
    return g_fastept.initPanel(panel_type, (uint32_t)speed);
}

int32_t epdInitLights(wasm_exec_env_t exec_env, int32_t led1, int32_t led2)
{
    (void)exec_env;
    if (led1 < 0 || led1 > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_init_lights: led1 out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    if (led2 < -1 || led2 > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_init_lights: led2 out of range (-1 for unused or 0-255)");
        return kWasmErrInvalidArgument;
    }
    g_fastept.initLights((uint8_t)led1, (uint8_t)(led2 < 0 ? 0xff : led2));
    return kWasmOk;
}

int32_t epdSetBrightness(wasm_exec_env_t exec_env, int32_t led1, int32_t led2)
{
    (void)exec_env;
    if (led1 < 0 || led1 > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_brightness: led1 out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    if (led2 < 0 || led2 > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_brightness: led2 out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    g_fastept.setBrightness((uint8_t)led1, (uint8_t)led2);
    return kWasmOk;
}

int32_t epdSetMode(wasm_exec_env_t exec_env, int32_t mode)
{
    (void)exec_env;
    if (mode < BB_MODE_NONE || mode > BB_MODE_4BPP) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_mode: mode out of range");
        return kWasmErrInvalidArgument;
    }
    return g_fastept.setMode(mode);
}

int32_t epdSetPanelSizePreset(wasm_exec_env_t exec_env, int32_t panel_id)
{
    (void)exec_env;
    if (panel_id < 0 || panel_id >= BB_PANEL_COUNT) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_panel_size_preset: panel_id out of range");
        return kWasmErrInvalidArgument;
    }
    return g_fastept.setPanelSize(panel_id);
}

int32_t epdSetPanelSize(wasm_exec_env_t exec_env, int32_t width, int32_t height, int32_t flags, int32_t vcom_mv)
{
    (void)exec_env;
    if (width <= 0 || height <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_panel_size: width/height must be > 0");
        return kWasmErrInvalidArgument;
    }
    return g_fastept.setPanelSize(width, height, flags, vcom_mv);
}

int32_t epdSetCustomMatrix(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    if (!ptr) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_custom_matrix: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_custom_matrix: len is 0");
        return kWasmErrInvalidArgument;
    }
    if ((len & 15u) != 0u) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_custom_matrix: len must be divisible by 16");
        return kWasmErrInvalidArgument;
    }

    uint8_t *matrix_copy = (uint8_t *)malloc(len);
    if (!matrix_copy) {
        wasm_api_set_last_error(kWasmErrInternal, "epd_set_custom_matrix: out of memory");
        return kWasmErrInternal;
    }
    memcpy(matrix_copy, ptr, len);

    const int rc = g_fastept.setCustomMatrix(matrix_copy, len);
    if (rc != BBEP_SUCCESS) {
        free(matrix_copy);
        return rc;
    }

    if (g_custom_matrix) {
        free(g_custom_matrix);
    }
    g_custom_matrix = matrix_copy;
    g_custom_matrix_size = len;
    return rc;
}

int32_t epdGetMode(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return g_fastept.getMode();
}

int32_t epdWidth(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return g_fastept.width();
}

int32_t epdHeight(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return g_fastept.height();
}

int32_t epdGetRotation(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return g_fastept.getRotation();
}

int32_t epdSetRotation(wasm_exec_env_t exec_env, int32_t rotation)
{
    (void)exec_env;
    if (rotation < 0 || rotation > 3) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_rotation: rotation out of range (0-3)");
        return kWasmErrInvalidArgument;
    }
    return g_fastept.setRotation(rotation);
}

int32_t epdFillScreen(wasm_exec_env_t exec_env, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_fill_screen: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.fillScreen((uint8_t)color);
    return kWasmOk;
}

int32_t epdDrawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_draw_pixel: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.drawPixel(x, y, (uint8_t)color);
    return kWasmOk;
}

int32_t epdDrawPixelFast(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_draw_pixel_fast: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    const int32_t w = g_fastept.width();
    const int32_t h = g_fastept.height();
    if (x < 0 || y < 0 || x >= w || y >= h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_draw_pixel_fast: coordinates out of bounds");
        return kWasmErrInvalidArgument;
    }
    g_fastept.drawPixelFast(x, y, (uint8_t)color);
    return kWasmOk;
}

int32_t epdDrawLine(wasm_exec_env_t exec_env, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_draw_line: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.drawLine(x1, y1, x2, y2, (uint32_t)color);
    return kWasmOk;
}

int32_t epdDrawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color)
{
    (void)exec_env;
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_draw_rect: negative dimensions");
        return kWasmErrInvalidArgument;
    }
    int32_t rc = validate_grayscale_color(color, "epd_draw_rect: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.drawRect(x, y, w, h, (uint8_t)color);
    return kWasmOk;
}

int32_t epdFillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color)
{
    (void)exec_env;
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_fill_rect: negative dimensions");
        return kWasmErrInvalidArgument;
    }
    int32_t rc = validate_grayscale_color(color, "epd_fill_rect: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.fillRect(x, y, w, h, (uint8_t)color);
    return kWasmOk;
}

int32_t epdDrawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_draw_circle: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.drawCircle(x, y, r, (uint32_t)color);
    return kWasmOk;
}

int32_t epdFillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_fill_circle: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.fillCircle(x, y, r, (uint32_t)color);
    return kWasmOk;
}

int32_t epdDrawRoundRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t color)
{
    (void)exec_env;
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_draw_round_rect: negative dimensions");
        return kWasmErrInvalidArgument;
    }
    int32_t rc = validate_grayscale_color(color, "epd_draw_round_rect: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.drawRoundRect(x, y, w, h, r, (uint8_t)color);
    return kWasmOk;
}

int32_t epdFillRoundRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t color)
{
    (void)exec_env;
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_fill_round_rect: negative dimensions");
        return kWasmErrInvalidArgument;
    }
    int32_t rc = validate_grayscale_color(color, "epd_fill_round_rect: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.fillRoundRect(x, y, w, h, r, (uint8_t)color);
    return kWasmOk;
}

int32_t epdDrawTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_draw_triangle: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.drawLine(x0, y0, x1, y1, (uint32_t)color);
    g_fastept.drawLine(x1, y1, x2, y2, (uint32_t)color);
    g_fastept.drawLine(x2, y2, x0, y0, (uint32_t)color);
    return kWasmOk;
}

int32_t epdFillTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_fill_triangle: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    int32_t min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int32_t max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int32_t min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int32_t max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    g_fastept.fillRect(min_x, min_y, max_x - min_x, max_y - min_y, (uint8_t)color);
    return kWasmOk;
}

int32_t epdSetTextColor(wasm_exec_env_t exec_env, int32_t fg, int32_t bg)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(fg, "epd_set_text_color: fg out of range (0-255)");
    if (rc != kWasmOk) return rc;
    rc = validate_optional_color(bg, "epd_set_text_color: bg out of range (-1 for transparent or 0-255)");
    if (rc != kWasmOk) return rc;
    g_fastept.setTextColor(fg, map_optional_color_to_fastepd(bg));
    return kWasmOk;
}

int32_t epdSetCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    (void)exec_env;
    g_fastept.setCursor(x, y);
    return kWasmOk;
}

int32_t epdSetFont(wasm_exec_env_t exec_env, int32_t font)
{
    (void)exec_env;
    if (font < 0 || font >= FONT_COUNT) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_font: font out of range");
        return kWasmErrInvalidArgument;
    }
    g_fastept.setFont(font);
    return kWasmOk;
}

int32_t epdSetTextWrap(wasm_exec_env_t exec_env, int32_t wrap)
{
    (void)exec_env;
    g_fastept.setTextWrap(wrap != 0);
    return kWasmOk;
}

int32_t epdDrawString(wasm_exec_env_t exec_env, const char *text, int32_t x, int32_t y)
{
    (void)exec_env;
    if (!text) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_draw_string: text is null");
        return kWasmErrInvalidArgument;
    }
    g_fastept.setCursor(x, y);
    g_fastept.drawString(text, x, y);
    return kWasmOk;
}

int32_t epdGetStringBox(wasm_exec_env_t exec_env, const char *text, uint8_t *out, int32_t out_len)
{
    (void)exec_env;
    if (!text) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_get_string_box: text is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_get_string_box: out_len < 0");
        return kWasmErrInvalidArgument;
    }
    if (!out && out_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_get_string_box: out is null");
        return kWasmErrInvalidArgument;
    }
    static_assert(sizeof(BB_RECT) == 16, "BB_RECT layout must stay stable");
    if ((size_t)out_len < sizeof(BB_RECT)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_get_string_box: out_len too small");
        return kWasmErrInvalidArgument;
    }

    BB_RECT rect = {};
    const int rc = g_fastept.getStringBox(text, &rect);
    if (rc != BBEP_SUCCESS) {
        wasm_api_set_last_error(kWasmErrInternal, "epd_get_string_box: getStringBox failed");
        return kWasmErrInternal;
    }
    memcpy(out, &rect, sizeof(rect));
    return (int32_t)sizeof(rect);
}

int32_t epdFullUpdate(wasm_exec_env_t exec_env, int32_t clear_mode, int32_t keep_on)
{
    (void)exec_env;
    if (clear_mode < CLEAR_NONE || clear_mode > CLEAR_BLACK) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_full_update: clear_mode out of range");
        return kWasmErrInvalidArgument;
    }
    return g_fastept.fullUpdate(clear_mode, keep_on != 0);
}

int32_t epdFullUpdateRect(wasm_exec_env_t exec_env, int32_t clear_mode, int32_t keep_on, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)exec_env;
    if (clear_mode < CLEAR_NONE || clear_mode > CLEAR_BLACK) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_full_update_rect: clear_mode out of range");
        return kWasmErrInvalidArgument;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_full_update_rect: negative dimensions");
        return kWasmErrInvalidArgument;
    }
    BB_RECT rect = {.x = x, .y = y, .w = w, .h = h};
    return g_fastept.fullUpdate(clear_mode, keep_on != 0, &rect);
}

int32_t epdPartialUpdate(wasm_exec_env_t exec_env, int32_t keep_on, int32_t start_row, int32_t end_row)
{
    (void)exec_env;
    return g_fastept.partialUpdate(keep_on != 0, start_row, end_row);
}

int32_t epdSmoothUpdate(wasm_exec_env_t exec_env, int32_t keep_on, int32_t color)
{
    (void)exec_env;
    int32_t rc = validate_grayscale_color(color, "epd_smooth_update: color out of range (0-255)");
    if (rc != kWasmOk) return rc;
    return g_fastept.smoothUpdate(keep_on != 0, (uint8_t)color);
}

int32_t epdClearWhite(wasm_exec_env_t exec_env, int32_t keep_on)
{
    (void)exec_env;
    return g_fastept.clearWhite(keep_on != 0);
}

int32_t epdClearBlack(wasm_exec_env_t exec_env, int32_t keep_on)
{
    (void)exec_env;
    return g_fastept.clearBlack(keep_on != 0);
}

int32_t epdBackupPlane(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    g_fastept.backupPlane();
    return kWasmOk;
}

int32_t epdInvertRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)exec_env;
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_invert_rect: negative dimensions");
        return kWasmErrInvalidArgument;
    }
    g_fastept.invertRect(x, y, w, h);
    return kWasmOk;
}

int32_t epdIoPinMode(wasm_exec_env_t exec_env, int32_t pin, int32_t mode)
{
    (void)exec_env;
    if (pin < 0 || pin > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_io_pin_mode: pin out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    if (mode < 0 || mode > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_io_pin_mode: mode out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    g_fastept.ioPinMode((uint8_t)pin, (uint8_t)mode);
    return kWasmOk;
}

int32_t epdIoWrite(wasm_exec_env_t exec_env, int32_t pin, int32_t value)
{
    (void)exec_env;
    if (pin < 0 || pin > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_io_write: pin out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    if (value < 0 || value > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_io_write: value out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    g_fastept.ioWrite((uint8_t)pin, (uint8_t)value);
    return kWasmOk;
}

int32_t epdIoRead(wasm_exec_env_t exec_env, int32_t pin)
{
    (void)exec_env;
    if (pin < 0 || pin > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_io_read: pin out of range (0-255)");
        return kWasmErrInvalidArgument;
    }
    return (int32_t)g_fastept.ioRead((uint8_t)pin);
}

int32_t epdEinkPower(wasm_exec_env_t exec_env, int32_t on)
{
    (void)exec_env;
    return g_fastept.einkPower(on != 0);
}

int32_t epdLoadBmp(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y, int32_t fg, int32_t bg)
{
    (void)exec_env;
    if (!ptr) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_bmp: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len < 30) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_bmp: len too small");
        return kWasmErrInvalidArgument;
    }
    if (x < 0 || y < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_bmp: negative coordinates");
        return kWasmErrInvalidArgument;
    }
    int32_t rc = validate_optional_color(fg, "epd_load_bmp: fg out of range (-1 for transparent or 0-255)");
    if (rc != kWasmOk) return rc;
    rc = validate_optional_color(bg, "epd_load_bmp: bg out of range (-1 for transparent or 0-255)");
    if (rc != kWasmOk) return rc;

    const uint16_t marker = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
    if (marker != 0x4d42) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_bmp: invalid BMP marker");
        return kWasmErrInvalidArgument;
    }

    const uint16_t cx = (uint16_t)ptr[18] | ((uint16_t)ptr[19] << 8);
    const int16_t cy_signed = (int16_t)((uint16_t)ptr[22] | ((uint16_t)ptr[23] << 8));
    const uint16_t cy = (uint16_t)(cy_signed < 0 ? -cy_signed : cy_signed);

    const uint16_t bpp = (uint16_t)ptr[28] | ((uint16_t)ptr[29] << 8);
    if (bpp != 1) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_bmp: only 1-bpp BMP supported");
        return kWasmErrInvalidArgument;
    }

    // Mirror FastEPD's offset parsing (low-byte offset only).
    const uint32_t off_bits = (uint32_t)ptr[10] + (uint32_t)ptr[11];
    const uint32_t pitch = ((((uint32_t)cx + 7u) >> 3u) + 3u) & ~3u;
    const uint64_t required = (uint64_t)off_bits + ((uint64_t)pitch * (uint64_t)cy);
    if (required > (uint64_t)len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_bmp: len smaller than BMP image data");
        return kWasmErrInvalidArgument;
    }

    return g_fastept.loadBMP(ptr, x, y, (int)map_optional_color_to_fastepd(fg), (int)map_optional_color_to_fastepd(bg));
}

int32_t epdLoadG5Image(
    wasm_exec_env_t exec_env,
    const uint8_t *ptr,
    size_t len,
    int32_t x,
    int32_t y,
    int32_t fg,
    int32_t bg,
    float scale)
{
    (void)exec_env;
    if (!ptr) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_g5_image: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len < 8) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_g5_image: len too small");
        return kWasmErrInvalidArgument;
    }
    if (x < 0 || y < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_g5_image: negative coordinates");
        return kWasmErrInvalidArgument;
    }
    if (scale < 0.01f) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_g5_image: scale too small");
        return kWasmErrInvalidArgument;
    }
    int32_t rc = validate_optional_color(fg, "epd_load_g5_image: fg out of range (-1 for transparent or 0-255)");
    if (rc != kWasmOk) return rc;
    rc = validate_optional_color(bg, "epd_load_g5_image: bg out of range (-1 for transparent or 0-255)");
    if (rc != kWasmOk) return rc;

    const uint16_t marker = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
    if (marker != 0xBBBF) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_g5_image: invalid G5 marker");
        return kWasmErrInvalidArgument;
    }
    const uint16_t size = (uint16_t)ptr[6] | ((uint16_t)ptr[7] << 8);
    const uint64_t required = 8u + (uint64_t)size;
    if (required > (uint64_t)len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_load_g5_image: len smaller than G5 data");
        return kWasmErrInvalidArgument;
    }

    return g_fastept.loadG5Image(
        ptr,
        x,
        y,
        (int)map_optional_color_to_fastepd(fg),
        (int)map_optional_color_to_fastepd(bg),
        scale);
}

int32_t epdSetPasses(wasm_exec_env_t exec_env, int32_t partial_passes, int32_t full_passes)
{
    (void)exec_env;
    if (partial_passes < 0 || partial_passes > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_passes: partial_passes out of range");
        return kWasmErrInvalidArgument;
    }
    if (full_passes < 0 || full_passes > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "epd_set_passes: full_passes out of range");
        return kWasmErrInvalidArgument;
    }
    g_fastept.setPasses((uint8_t)partial_passes, (uint8_t)full_passes);
    return kWasmOk;
}

int32_t epdDeinit(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    g_fastept.deInit();
    bbepDeinitBus();
    if (g_custom_matrix) {
        free(g_custom_matrix);
        g_custom_matrix = nullptr;
        g_custom_matrix_size = 0;
    }
    ESP_LOGI(kTag, "FastEPD deinitialized and I80 bus released");
    return kWasmOk;
}

#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_fast_epd_native_symbols[] = {
    REG_NATIVE_FUNC(epdInitPanel, "(ii)i"),
    REG_NATIVE_FUNC(epdInitLights, "(ii)i"),
    REG_NATIVE_FUNC(epdSetBrightness, "(ii)i"),
    REG_NATIVE_FUNC(epdSetMode, "(i)i"),
    REG_NATIVE_FUNC(epdGetMode, "()i"),
    REG_NATIVE_FUNC(epdSetPanelSizePreset, "(i)i"),
    REG_NATIVE_FUNC(epdSetPanelSize, "(iiii)i"),
    REG_NATIVE_FUNC(epdSetCustomMatrix, "(*~)i"),
    REG_NATIVE_FUNC(epdWidth, "()i"),
    REG_NATIVE_FUNC(epdHeight, "()i"),
    REG_NATIVE_FUNC(epdGetRotation, "()i"),
    REG_NATIVE_FUNC(epdSetRotation, "(i)i"),
    REG_NATIVE_FUNC(epdFillScreen, "(i)i"),
    REG_NATIVE_FUNC(epdDrawPixel, "(iii)i"),
    REG_NATIVE_FUNC(epdDrawPixelFast, "(iii)i"),
    REG_NATIVE_FUNC(epdDrawLine, "(iiiii)i"),
    REG_NATIVE_FUNC(epdDrawRect, "(iiiii)i"),
    REG_NATIVE_FUNC(epdFillRect, "(iiiii)i"),
    REG_NATIVE_FUNC(epdDrawCircle, "(iiii)i"),
    REG_NATIVE_FUNC(epdFillCircle, "(iiii)i"),
    REG_NATIVE_FUNC(epdDrawRoundRect, "(iiiiii)i"),
    REG_NATIVE_FUNC(epdFillRoundRect, "(iiiiii)i"),
    REG_NATIVE_FUNC(epdDrawTriangle, "(iiiiiii)i"),
    REG_NATIVE_FUNC(epdFillTriangle, "(iiiiiii)i"),
    REG_NATIVE_FUNC(epdSetTextColor, "(ii)i"),
    REG_NATIVE_FUNC(epdSetCursor, "(ii)i"),
    REG_NATIVE_FUNC(epdSetFont, "(i)i"),
    REG_NATIVE_FUNC(epdSetTextWrap, "(i)i"),
    REG_NATIVE_FUNC(epdDrawString, "(*ii)i"),
    REG_NATIVE_FUNC(epdGetStringBox, "($*i)i"),
    REG_NATIVE_FUNC(epdFullUpdate, "(ii)i"),
    REG_NATIVE_FUNC(epdFullUpdateRect, "(iiiiii)i"),
    REG_NATIVE_FUNC(epdPartialUpdate, "(iii)i"),
    REG_NATIVE_FUNC(epdSmoothUpdate, "(ii)i"),
    REG_NATIVE_FUNC(epdClearWhite, "(i)i"),
    REG_NATIVE_FUNC(epdClearBlack, "(i)i"),
    REG_NATIVE_FUNC(epdBackupPlane, "()i"),
    REG_NATIVE_FUNC(epdInvertRect, "(iiii)i"),
    REG_NATIVE_FUNC(epdIoPinMode, "(ii)i"),
    REG_NATIVE_FUNC(epdIoWrite, "(ii)i"),
    REG_NATIVE_FUNC(epdIoRead, "(i)i"),
    REG_NATIVE_FUNC(epdEinkPower, "(i)i"),
    REG_NATIVE_FUNC(epdLoadBmp, "(*~iiii)i"),
    REG_NATIVE_FUNC(epdLoadG5Image, "(*~iiiif)i"),
    REG_NATIVE_FUNC(epdSetPasses, "(ii)i"),
    REG_NATIVE_FUNC(epdDeinit, "()i"),
};

}

bool wasm_api_register_fast_epd(void)
{
    const uint32_t count = sizeof(g_fast_epd_native_symbols) / sizeof(g_fast_epd_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("fast_epd", g_fast_epd_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register fast_epd natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_fast_epd: wasm_runtime_register_natives failed");
    }
    return ok;
}
