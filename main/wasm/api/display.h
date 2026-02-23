#pragma once

#include <memory>
#include <stddef.h>
#include <stdint.h>

#include "wasm_export.h"

enum class PaperDisplayDriver : int32_t {
    none = -1,
    lgfx = 0,
    fastepd = 1,
};

enum class PaperIcon : int32_t {
    battery = 0,
    devserver = 1,
    softap = 2,
    wifi = 3,
    count = 4,
};

constexpr int32_t kVlwSystemFontInter = 0;
constexpr int32_t kVlwSystemFontMontserrat = 1;

class Display {
public:
    static Display* current();
    static void setCurrent(PaperDisplayDriver driver);
    virtual PaperDisplayDriver driver() = 0;
    virtual bool init() = 0;

    virtual ~Display() = default;


    // display.cpp
    virtual int32_t release(wasm_exec_env_t exec_env) = 0;
    virtual int32_t width(wasm_exec_env_t exec_env) = 0;
    virtual int32_t height(wasm_exec_env_t exec_env) = 0;
    virtual int32_t getRotation(wasm_exec_env_t exec_env) = 0;
    virtual int32_t setRotation(wasm_exec_env_t exec_env, int32_t rot) = 0;
    virtual int32_t clear(wasm_exec_env_t exec_env) = 0;
    virtual int32_t fillScreen(wasm_exec_env_t exec_env, int32_t rgb888) = 0;
    virtual int32_t display(wasm_exec_env_t exec_env) = 0;
    virtual int32_t displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h) = 0;
    virtual int32_t fullUpdateSlow(wasm_exec_env_t exec_env)
    {
        return this->display(exec_env);
    }
    virtual int32_t waitDisplay(wasm_exec_env_t exec_env) = 0;
    virtual int32_t startWrite(wasm_exec_env_t exec_env) = 0;
    virtual int32_t endWrite(wasm_exec_env_t exec_env) = 0;
    virtual int32_t setBrightness(wasm_exec_env_t exec_env, int32_t v) = 0;
    virtual int32_t getBrightness(wasm_exec_env_t exec_env) = 0;
    virtual int32_t setEpdMode(wasm_exec_env_t exec_env, int32_t mode) = 0;
    virtual int32_t getEpdMode(wasm_exec_env_t exec_env) = 0;

    // display_text.cpp
    virtual int32_t setCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y) = 0;
    virtual int32_t setTextSize(wasm_exec_env_t exec_env, float sx, float sy) = 0;
    virtual int32_t setTextDatum(wasm_exec_env_t exec_env, int32_t datum) = 0;
    virtual int32_t setTextColor(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg) = 0;
    virtual int32_t setTextWrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y) = 0;
    virtual int32_t setTextScroll(wasm_exec_env_t exec_env, int32_t scroll) = 0;
    virtual int32_t setTextFont(wasm_exec_env_t exec_env, int32_t font_id) = 0;
    virtual int32_t setTextEncoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable) = 0;
    virtual int32_t drawString(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y) = 0;
    virtual int32_t textWidth(wasm_exec_env_t exec_env, const char *s) = 0;
    virtual int32_t fontHeight(wasm_exec_env_t exec_env) = 0;
    virtual int32_t vlwRegister(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len) = 0;
    virtual int32_t vlwUse(wasm_exec_env_t exec_env, int32_t handle) = 0;
    virtual int32_t vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id, int32_t font_size) = 0;
    virtual int32_t vlwUnload(wasm_exec_env_t exec_env) = 0;
    virtual int32_t vlwClearAll(wasm_exec_env_t exec_env) = 0;

    // display_images.cpp
    virtual int32_t pushImageRgb565(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        const uint8_t *ptr,
        size_t len) = 0;
    virtual int32_t pushImage(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        const uint8_t *data_ptr,
        size_t data_len,
        int32_t depth_raw,
        const uint8_t *palette_ptr,
        size_t palette_len) = 0;
    virtual int32_t pushImageGray8(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        const uint8_t *ptr,
        size_t len) = 0;
    virtual int32_t readRectRgb565(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        uint8_t *out,
        size_t out_len) = 0;
    virtual int32_t drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y) = 0;
    virtual int32_t drawXthCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len) = 0;
    virtual int32_t drawXtgCentered(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len) = 0;
    virtual int32_t drawJpgFit(
        wasm_exec_env_t exec_env,
        const uint8_t *ptr,
        size_t len,
        int32_t x,
        int32_t y,
        int32_t max_w,
        int32_t max_h) = 0;
    virtual int32_t drawPngFit(
        wasm_exec_env_t exec_env,
        const uint8_t *ptr,
        size_t len,
        int32_t x,
        int32_t y,
        int32_t max_w,
        int32_t max_h) = 0;
    virtual int32_t drawJpgFile(
        wasm_exec_env_t exec_env,
        const char *path,
        int32_t x,
        int32_t y,
        int32_t max_w,
        int32_t max_h) = 0;
    virtual int32_t drawPngFile(
        wasm_exec_env_t exec_env,
        const char *path,
        int32_t x,
        int32_t y,
        int32_t max_w,
        int32_t max_h) = 0;

    // display_primitives.cpp
    virtual int32_t drawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888) = 0;
    virtual int32_t drawLine(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t rgb888) = 0;
    virtual int32_t drawFastVline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888) = 0;
    virtual int32_t drawFastHline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888) = 0;
    virtual int32_t drawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888) = 0;
    virtual int32_t fillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888) = 0;
    virtual int32_t drawRoundRect(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        int32_t r,
        int32_t rgb888) = 0;
    virtual int32_t fillRoundRect(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t w,
        int32_t h,
        int32_t r,
        int32_t rgb888) = 0;
    virtual int32_t drawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888) = 0;
    virtual int32_t fillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888) = 0;
    virtual int32_t fillArc(
        wasm_exec_env_t exec_env,
        int32_t x,
        int32_t y,
        int32_t r0,
        int32_t r1,
        float angle0,
        float angle1,
        int32_t rgb888) = 0;
    virtual int32_t drawEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888) = 0;
    virtual int32_t fillEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rgb888) = 0;
    virtual int32_t drawTriangle(
        wasm_exec_env_t exec_env,
        int32_t x0,
        int32_t y0,
        int32_t x1,
        int32_t y1,
        int32_t x2,
        int32_t y2,
        int32_t rgb888) = 0;
    virtual int32_t fillTriangle(
        wasm_exec_env_t exec_env,
        int32_t x0,
        int32_t y0,
        int32_t x1,
        int32_t y1,
        int32_t x2,
        int32_t y2,
        int32_t rgb888) = 0;

    private:
        static std::unique_ptr<Display> _current;
};
