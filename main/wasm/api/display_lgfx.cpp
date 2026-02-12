#include "display_lgfx.h"

int32_t DisplayLgfx::width(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::height(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::getRotation(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::setRotation(wasm_exec_env_t exec_env, int32_t rot)
{
    return 0;
}

int32_t DisplayLgfx::clear(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::fillScreen(wasm_exec_env_t exec_env, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::display(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return 0;
}

int32_t DisplayLgfx::waitDisplay(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::startWrite(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::endWrite(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::setBrightness(wasm_exec_env_t exec_env, int32_t v)
{
    return 0;
}

int32_t DisplayLgfx::getBrightness(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::setEpdMode(wasm_exec_env_t exec_env, int32_t mode)
{
    return 0;
}

int32_t DisplayLgfx::getEpdMode(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::setCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    return 0;
}

int32_t DisplayLgfx::setTextSize(wasm_exec_env_t exec_env, float sx, float sy)
{
    return 0;
}

int32_t DisplayLgfx::setTextDatum(wasm_exec_env_t exec_env, int32_t datum)
{
    return 0;
}

int32_t DisplayLgfx::setTextColor(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg)
{
    return 0;
}

int32_t DisplayLgfx::setTextWrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y)
{
    return 0;
}

int32_t DisplayLgfx::setTextScroll(wasm_exec_env_t exec_env, int32_t scroll)
{
    return 0;
}

int32_t DisplayLgfx::setTextFont(wasm_exec_env_t exec_env, int32_t font_id)
{
    return 0;
}

int32_t DisplayLgfx::setTextEncoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable)
{
    return 0;
}

int32_t DisplayLgfx::drawString(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y)
{
    return 0;
}

int32_t DisplayLgfx::textWidth(wasm_exec_env_t exec_env, const char *s)
{
    return 0;
}

int32_t DisplayLgfx::fontHeight(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::vlwRegister(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    return 0;
}

int32_t DisplayLgfx::vlwUse(wasm_exec_env_t exec_env, int32_t handle)
{
    return 0;
}

int32_t DisplayLgfx::vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id)
{
    return 0;
}

int32_t DisplayLgfx::vlwUnload(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::vlwClearAll(wasm_exec_env_t exec_env)
{
    return 0;
}

int32_t DisplayLgfx::pushImageRgb565(
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

int32_t DisplayLgfx::pushImage(
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

int32_t DisplayLgfx::pushImageGray8(
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

int32_t DisplayLgfx::readRectRgb565(
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

int32_t DisplayLgfx::drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    return 0;
}

int32_t DisplayLgfx::drawXthCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    return 0;
}

int32_t DisplayLgfx::drawXtgCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    return 0;
}

int32_t DisplayLgfx::drawJpgFit(
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

int32_t DisplayLgfx::drawPngFit(
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

int32_t DisplayLgfx::drawJpgFile(
    wasm_exec_env_t exec_env,
    const char *path,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    return 0;
}

int32_t DisplayLgfx::drawPngFile(
    wasm_exec_env_t exec_env,
    const char *path,
    int32_t x,
    int32_t y,
    int32_t max_w,
    int32_t max_h)
{
    return 0;
}

int32_t DisplayLgfx::drawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::drawLine(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::drawFastVline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::drawFastHline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::drawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::fillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::drawRoundRect(
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

int32_t DisplayLgfx::fillRoundRect(
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

int32_t DisplayLgfx::drawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::fillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::fillArc(
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

int32_t DisplayLgfx::drawEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::fillEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888)
{
    return 0;
}

int32_t DisplayLgfx::drawTriangle(
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

int32_t DisplayLgfx::fillTriangle(
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
