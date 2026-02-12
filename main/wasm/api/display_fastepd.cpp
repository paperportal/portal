#include "display.h"

int32_t DisplayFastEpd::width(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::height(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::getRotation(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::setRotation(wasm_exec_env_t exec_env, int32_t rot)
{
    return 0;
}

int32_t DisplayFastEpd::clear(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::fillScreen(wasm_exec_env_t exec_env, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::display(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return 0;
}

int32_t DisplayFastEpd::waitDisplay(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::startWrite(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::endWrite(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::setBrightness(wasm_exec_env_t exec_env, int32_t v)
{
    return 0;
}

int32_t DisplayFastEpd::getBrightness(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::setEpdMode(wasm_exec_env_t exec_env, int32_t mode)
{
    return 0;
}

int32_t DisplayFastEpd::getEpdMode(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::setCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    return 0;
}

int32_t DisplayFastEpd::setTextSize(wasm_exec_env_t exec_env, float sx, float sy)
{
    return 0;
}

int32_t DisplayFastEpd::setTextDatum(wasm_exec_env_t exec_env, int32_t datum)
{
    return 0;
}

int32_t DisplayFastEpd::setTextColor(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg)
{
    return 0;
}

int32_t DisplayFastEpd::setTextWrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y)
{
    return 0;
}

int32_t DisplayFastEpd::setTextScroll(wasm_exec_env_t exec_env, int32_t scroll)
{
    return 0;
}

int32_t DisplayFastEpd::setTextFont(wasm_exec_env_t exec_env, int32_t font_id)
{
    return 0;
}

int32_t DisplayFastEpd::setTextEncoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable)
{
    return 0;
}

int32_t DisplayFastEpd::drawString(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y)
{
    return 0;
}

int32_t DisplayFastEpd::textWidth(wasm_exec_env_t exec_env, const char *s)
{
    return 0;
}

int32_t DisplayFastEpd::fontHeight(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::vlwRegister(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    return 0;
}

int32_t DisplayFastEpd::vlwUse(wasm_exec_env_t exec_env, int32_t handle)
{
    return 0;
}

int32_t DisplayFastEpd::vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id)
{
    return 0;
}

int32_t DisplayFastEpd::vlwUnload(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::vlwClearAll(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayFastEpd::pushImageRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    return 0;
}

int32_t DisplayFastEpd::pushImage(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *data_ptr,
    size_t data_len,
    int32_t depth_raw,
    const uint8_t *palette_ptr,
    size_t palette_len)
{
    return 0;
}

int32_t DisplayFastEpd::pushImageGray8(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    const uint8_t *ptr,
    size_t len)
{
    return 0;
}

int32_t DisplayFastEpd::readRectRgb565(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    uint8_t *out,
    size_t out_len)
{
    return 0;
}

int32_t DisplayFastEpd::drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    return 0;
}

int32_t DisplayFastEpd::drawXthCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    return 0;
}

int32_t DisplayFastEpd::drawXtgCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    return 0;
}

int32_t DisplayFastEpd::drawJpgFit(
    wasm_exec_env_t exec_env,
    const uint8_t *ptr,
    size_t len,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    return 0;
}

int32_t DisplayFastEpd::drawPngFit(
    wasm_exec_env_t exec_env,
    const uint8_t *ptr,
    size_t len,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    return 0;
}

int32_t DisplayFastEpd::drawJpgFile(
    wasm_exec_env_t exec_env,
    const char *path,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    return 0;
}

int32_t DisplayFastEpd::drawPngFile(
    wasm_exec_env_t exec_env,
    const char *path,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    return 0;
}

int32_t DisplayFastEpd::drawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawLine(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawFastVline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawFastHline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::fillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawRoundRect(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    int32_t r,
    int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::fillRoundRect(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    int32_t r,
    int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::fillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::fillArc(
    wasm_exec_env_t exec_env,
    int32_t x,
    int32_t y,
    int32_t r0,
    int32_t r1,
    float angle0,
    float angle1,
    int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::fillEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::drawTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    return 0;
}

int32_t DisplayFastEpd::fillTriangle(
    wasm_exec_env_t exec_env,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t x2,
    int32_t y2,
    int32_t rgb888)
{
    return 0;
}
