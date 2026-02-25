#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mutex>
#include <vector>

#include "display_lgfx.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "m5papers3_display.h"
#include "other/lgfx_xtc.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "display_lgfx";

constexpr size_t kMaxPngBytes = 1024 * 1024;
constexpr size_t kMaxJpgBytes = 1024 * 1024;
constexpr size_t kMaxXthBytes = 1024 * 1024;
constexpr size_t kMaxXtgBytes = 1024 * 1024;
constexpr size_t kMaxVlwBytes = 1024 * 1024;

extern const uint8_t _binary_inter_medium_32_vlw_start[] asm("_binary_inter_medium_32_vlw_start");
extern const uint8_t _binary_inter_medium_32_vlw_end[] asm("_binary_inter_medium_32_vlw_end");
extern const uint8_t _binary_montserrat_light_20_vlw_start[] asm("_binary_montserrat_light_20_vlw_start");
extern const uint8_t _binary_montserrat_light_20_vlw_end[] asm("_binary_montserrat_light_20_vlw_end");

struct FontBlob {
    uint8_t *data = nullptr;
    size_t len = 0;
};

std::mutex g_font_mutex;
std::vector<FontBlob> g_fonts;
bool g_lgfx_inited = false;
int32_t g_display_mode = 3; // default: GRAY256 (8bpp) set by LGFX_M5PaperS3 ctor

LGFX_M5PaperS3 *get_display_or_set_error(void)
{
    if (!paper_display_ensure_init()) {
        wasm_api_set_last_error(kWasmErrNotReady, "display not ready (init failed)");
        return nullptr;
    }
    return &paper_display();
}

uint8_t *alloc_font_bytes(size_t len)
{
    if (len == 0) {
        return nullptr;
    }
    void *ptr = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_malloc(len, MALLOC_CAP_8BIT);
    }
    return static_cast<uint8_t *>(ptr);
}

uint32_t color_from_rgb888(int32_t rgb888)
{
    const uint32_t raw = (uint32_t)rgb888;
    const uint8_t r = (uint8_t)((raw >> 16) & 0xFF);
    const uint8_t g = (uint8_t)((raw >> 8) & 0xFF);
    const uint8_t b = (uint8_t)(raw & 0xFF);
    return lgfx::color888(r, g, b);
}

bool validate_display_rect(const LGFX_M5PaperS3 &display, int32_t x, int32_t y, int32_t w, int32_t h,
    const char *context)
{
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }

    const int32_t max_w = (int32_t)display.width();
    const int32_t max_h = (int32_t)display.height();
    const int64_t x2 = (int64_t)x + (int64_t)w;
    const int64_t y2 = (int64_t)y + (int64_t)h;
    if (x2 > max_w || y2 > max_h) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, context);
        return false;
    }

    return true;
}

bool is_valid_text_datum(int32_t datum)
{
    switch (datum) {
    case 0:  // top_left
    case 1:  // top_center
    case 2:  // top_right
    case 4:  // middle_left
    case 5:  // middle_center
    case 6:  // middle_right
    case 8:  // bottom_left
    case 9:  // bottom_center
    case 10: // bottom_right
    case 16: // baseline_left
    case 17: // baseline_center
    case 18: // baseline_right
        return true;
    default:
        return false;
    }
}

bool canonicalize_color_depth(int32_t depth_raw, lgfx::color_depth_t *out_depth, uint32_t *out_bits,
    bool *out_requires_palette, uint32_t *out_palette_entries)
{
    if (!out_depth || !out_bits || !out_requires_palette || !out_palette_entries) {
        wasm_api_set_last_error(kWasmErrInternal, "canonicalize_color_depth: null out pointer");
        return false;
    }

    const uint16_t raw_u16 = (uint16_t)depth_raw;
    const uint32_t bits = (uint32_t)(raw_u16 & (uint16_t)lgfx::color_depth_t::bit_mask);
    const bool has_palette = (raw_u16 & (uint16_t)lgfx::color_depth_t::has_palette) != 0;
    const bool nonswapped = (raw_u16 & (uint16_t)lgfx::color_depth_t::nonswapped) != 0;
    const bool alternate = (raw_u16 & (uint16_t)lgfx::color_depth_t::alternate) != 0;

    lgfx::color_depth_t depth = lgfx::color_depth_t::rgb565_2Byte;
    uint32_t palette_entries = 0;

    switch (bits) {
        case 1:
            depth = has_palette ? lgfx::color_depth_t::palette_1bit : lgfx::color_depth_t::grayscale_1bit;
            palette_entries = 2;
            break;
        case 2:
            depth = has_palette ? lgfx::color_depth_t::palette_2bit : lgfx::color_depth_t::grayscale_2bit;
            palette_entries = 4;
            break;
        case 4:
            depth = has_palette ? lgfx::color_depth_t::palette_4bit : lgfx::color_depth_t::grayscale_4bit;
            palette_entries = 16;
            break;
        case 8:
            if (has_palette) {
                depth = lgfx::color_depth_t::palette_8bit;
                palette_entries = 256;
            } else {
                depth = alternate ? lgfx::color_depth_t::grayscale_8bit : lgfx::color_depth_t::rgb332_1Byte;
            }
            break;
        case 16:
            depth = nonswapped ? lgfx::color_depth_t::rgb565_nonswapped : lgfx::color_depth_t::rgb565_2Byte;
            break;
        case 24:
            if (alternate) {
                depth = nonswapped ? lgfx::color_depth_t::rgb666_nonswapped : lgfx::color_depth_t::rgb666_3Byte;
            } else {
                depth = nonswapped ? lgfx::color_depth_t::rgb888_nonswapped : lgfx::color_depth_t::rgb888_3Byte;
            }
            break;
        case 32:
            depth = nonswapped ? lgfx::color_depth_t::argb8888_nonswapped : lgfx::color_depth_t::argb8888_4Byte;
            break;
        default:
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: invalid color depth bit count");
            return false;
    }

    *out_depth = depth;
    *out_bits = bits;
    *out_palette_entries = palette_entries;
    *out_requires_palette = (palette_entries != 0) || (bits < 8);
    return true;
}

bool compute_expected_image_len(int32_t w, int32_t h, uint32_t bits, size_t *out_expected_len)
{
    if (!out_expected_len) {
        wasm_api_set_last_error(kWasmErrInternal, "compute_expected_image_len: out_expected_len is null");
        return false;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: negative size");
        return false;
    }

    const uint64_t pixels = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h;
    if (pixels > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: size overflow");
        return false;
    }

    uint64_t expected = 0;
    if (bits == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: bits is zero");
        return false;
    }
    if (bits < 8) {
        expected = (pixels * (uint64_t)bits + 7u) / 8u;
    } else {
        expected = pixels * ((uint64_t)bits / 8u);
    }

    if (expected > (uint64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: expected_len overflow");
        return false;
    }

    *out_expected_len = (size_t)expected;
    return true;
}

} // namespace

PaperDisplayDriver DisplayLgfx::driver() {
    return PaperDisplayDriver::lgfx;
}

bool DisplayLgfx::init()
{
    if (g_lgfx_inited) {
        return true;
    }

    ESP_LOGI(kTag, "Initializing LGFX display...");
    hold_pwroff_pulse_low();
    g_lgfx_inited = paper_display().init();
    if (!g_lgfx_inited) {
        ESP_LOGE(kTag, "LGFX init() failed");
        return false;
    }

    ESP_LOGI(kTag, "LGFX init OK: w=%d h=%d rotation=%d",
             static_cast<int>(paper_display().width()),
             static_cast<int>(paper_display().height()),
             static_cast<int>(paper_display().getRotation()));
    return true;
}

int32_t DisplayLgfx::release(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    if (g_lgfx_inited) {
        ESP_LOGI(kTag, "release: deinitializing LGFX display resources");
        auto &display = paper_display();
        display.unloadFont();
        auto *panel = static_cast<lgfx::Panel_EPD *>(display.getPanel());
// TODO        panel->deinit();
        g_lgfx_inited = false;
        ESP_LOGI(kTag, "release: LGFX EPD task+buffers+i80 bus released");
    }

    std::lock_guard<std::mutex> lock(g_font_mutex);
    for (auto &blob : g_fonts) {
        if (blob.data) {
            heap_caps_free(blob.data);
            blob.data = nullptr;
            blob.len = 0;
        }
    }
    g_fonts.clear();
    return kWasmOk;
}

int32_t DisplayLgfx::width(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->width();
}

int32_t DisplayLgfx::height(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->height();
}

int32_t DisplayLgfx::getRotation(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getRotation();
}

int32_t DisplayLgfx::setRotation(wasm_exec_env_t exec_env, int32_t rot)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rot < 0 || rot > 3) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setRotation: rot out of range (expected 0..3)");
        return kWasmErrInvalidArgument;
    }
    display->setRotation((uint_fast8_t)rot);
    return kWasmOk;
}

int32_t DisplayLgfx::setDisplayMode(wasm_exec_env_t exec_env, int32_t mode)
{
    (void)exec_env;
    if (mode < 0 || mode > 3) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setDisplayMode: mode out of range (expected 0..3)");
        return kWasmErrInvalidArgument;
    }
    if (mode == g_display_mode) {
        return kWasmOk;
    }

    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }

    lgfx::color_depth_t depth = lgfx::color_depth_t::grayscale_8bit;
    switch (mode) {
        case 0:
            depth = lgfx::color_depth_t::grayscale_1bit;
            break;
        case 1:
            depth = lgfx::color_depth_t::grayscale_2bit;
            break;
        case 2:
            depth = lgfx::color_depth_t::grayscale_4bit;
            break;
        case 3:
            depth = lgfx::color_depth_t::grayscale_8bit;
            break;
    }

    display->setColorDepth(depth);
    g_display_mode = mode;
    return kWasmOk;
}

int32_t DisplayLgfx::clear(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->clearDisplay();
    return kWasmOk;
}

int32_t DisplayLgfx::fillScreen(wasm_exec_env_t exec_env, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->fillScreen(color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::display(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->display();
    return kWasmOk;
}

int32_t DisplayLgfx::displayRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "displayRect: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }
    display->display(x, y, w, h);
    return kWasmOk;
}

int32_t DisplayLgfx::waitDisplay(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->waitDisplay();
    return kWasmOk;
}

int32_t DisplayLgfx::startWrite(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->startWrite();
    return kWasmOk;
}

int32_t DisplayLgfx::endWrite(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->endWrite();
    return kWasmOk;
}

int32_t DisplayLgfx::setBrightness(wasm_exec_env_t exec_env, int32_t v)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (v < 0 || v > 255) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setBrightness: v out of range (expected 0..255)");
        return kWasmErrInvalidArgument;
    }
    display->setBrightness((uint8_t)v);
    return kWasmOk;
}

int32_t DisplayLgfx::getBrightness(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getBrightness();
}

int32_t DisplayLgfx::setEpdMode(wasm_exec_env_t exec_env, int32_t mode)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (mode < 1 || mode > 4) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setEpdMode: mode out of range (1..4)");
        return kWasmErrInvalidArgument;
    }
    display->setEpdMode((lgfx::epd_mode_t)mode);
    return kWasmOk;
}

int32_t DisplayLgfx::getEpdMode(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->getEpdMode();
}

int32_t DisplayLgfx::setCursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setCursor(x, y);
    return kWasmOk;
}

int32_t DisplayLgfx::setTextSize(wasm_exec_env_t exec_env, float sx, float sy)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!(sx > 0.0f) || !(sy > 0.0f)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setTextSize: sx/sy must be > 0");
        return kWasmErrInvalidArgument;
    }
    display->setTextSize(sx, sy);
    return kWasmOk;
}

int32_t DisplayLgfx::setTextDatum(wasm_exec_env_t exec_env, int32_t datum)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!is_valid_text_datum(datum)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setTextDatum: invalid datum");
        return kWasmErrInvalidArgument;
    }
    display->setTextDatum((uint8_t)datum);
    return kWasmOk;
}

int32_t DisplayLgfx::setTextColor(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    const uint32_t fg = color_from_rgb888(fg_rgb888);
    if (use_bg) {
        const uint32_t bg = color_from_rgb888(bg_rgb888);
        display->setTextColor(fg, bg);
    }
    else {
        display->setTextColor(fg);
    }
    return kWasmOk;
}

int32_t DisplayLgfx::setTextWrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setTextWrap(wrap_x != 0, wrap_y != 0);
    return kWasmOk;
}

int32_t DisplayLgfx::setTextScroll(wasm_exec_env_t exec_env, int32_t scroll)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setTextScroll(scroll != 0);
    return kWasmOk;
}

int32_t DisplayLgfx::setTextFont(wasm_exec_env_t exec_env, int32_t font_id)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    switch (font_id) {
    case 0: display->setFont(&fonts::Font0); break;
    case 1: display->setFont(&fonts::AsciiFont8x16); break;
    case 2: display->setFont(&fonts::AsciiFont24x48); break;
    case 3: display->setFont(&fonts::TomThumb); break;
    default:
        wasm_api_set_last_error(kWasmErrInvalidArgument, "setTextFont: unknown font_id");
        return kWasmErrInvalidArgument;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::setTextEncoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setAttribute(lgfx::utf8_switch, (uint8_t)(utf8_enable != 0));
    display->setAttribute(lgfx::cp437_switch, (uint8_t)(cp437_enable != 0));
    return kWasmOk;
}

int32_t DisplayLgfx::drawString(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!s) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawString: s is null");
        return kWasmErrInvalidArgument;
    }
    const size_t w = display->drawString(s, x, y);
    if (w > (size_t)INT32_MAX) {
        wasm_api_set_last_error(kWasmErrInternal, "drawString: width overflow");
        return kWasmErrInternal;
    }
    return (int32_t)w;
}

int32_t DisplayLgfx::textWidth(wasm_exec_env_t exec_env, const char *s)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!s) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "textWidth: s is null");
        return kWasmErrInvalidArgument;
    }

    const int32_t w = (int32_t)display->textWidth(s);
    if (w < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "textWidth: negative width");
        return kWasmErrInternal;
    }
    return w;
}

int32_t DisplayLgfx::fontHeight(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->fontHeight();
}

int32_t DisplayLgfx::vlwRegister(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwRegister: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwRegister: len is 0");
        return kWasmErrInvalidArgument;
    }
    if (len > kMaxVlwBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwRegister: len too large");
        return kWasmErrInvalidArgument;
    }

    uint8_t *copy = alloc_font_bytes(len);
    if (!copy) {
        wasm_api_set_last_error(kWasmErrInternal, "vlwRegister: alloc failed");
        return kWasmErrInternal;
    }
    memcpy(copy, ptr, len);

    std::lock_guard<std::mutex> lock(g_font_mutex);
    const int32_t handle = (int32_t)g_fonts.size();
    g_fonts.push_back(FontBlob{copy, len});
    return handle;
}

int32_t DisplayLgfx::vlwUse(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    std::lock_guard<std::mutex> lock(g_font_mutex);
    if (handle < 0 || (size_t)handle >= g_fonts.size()) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwUse: invalid handle");
        return kWasmErrInvalidArgument;
    }
    FontBlob &blob = g_fonts[(size_t)handle];
    if (!blob.data || blob.len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwUse: font empty");
        return kWasmErrInvalidArgument;
    }
    display->unloadFont();
    bool ok = display->loadFont(blob.data);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "vlwUse: loadFont failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::vlwUseSystem(wasm_exec_env_t exec_env, int32_t font_id, int32_t font_size)
{
    auto *display = get_display_or_set_error();
    if (!display) {
        ESP_LOGI(kTag, "vlwUseSystem aborted: display not ready");
        return kWasmErrNotReady;
    }
    (void)font_size;

    const uint8_t *font_ptr = nullptr;
    const char *font_name = nullptr;
    size_t font_bytes = 0;
    switch (font_id) {
    case kVlwSystemFontInter:
        font_ptr = _binary_inter_medium_32_vlw_start;
        font_name = "inter_medium_32";
        font_bytes = (size_t)(_binary_inter_medium_32_vlw_end - _binary_inter_medium_32_vlw_start);
        break;
    case kVlwSystemFontMontserrat:
        font_ptr = _binary_montserrat_light_20_vlw_start;
        font_name = "montserrat_light_20";
        font_bytes = (size_t)(_binary_montserrat_light_20_vlw_end - _binary_montserrat_light_20_vlw_start);
        break;
    default:
        ESP_LOGI(kTag, "vlwUseSystem rejected invalid font_id=%" PRId32, font_id);
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlwUseSystem: invalid font_id");
        return kWasmErrInvalidArgument;
    }

    display->unloadFont();
    bool ok = display->loadFont(font_ptr);
    if (!ok) {
        ESP_LOGI(kTag, "vlwUseSystem failed to load font '%s' (font_id=%" PRId32 ")", font_name, font_id);
        wasm_api_set_last_error(kWasmErrInternal, "vlwUseSystem: loadFont failed");
        return kWasmErrInternal;
    }
    ESP_LOGI(kTag, "vlwUseSystem loaded font '%s' (%u bytes, font_id=%" PRId32 ")", font_name, (unsigned)font_bytes,
        font_id);
    return kWasmOk;
}

int32_t DisplayLgfx::vlwUnload(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->unloadFont();
    return kWasmOk;
}

int32_t DisplayLgfx::vlwClearAll(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->unloadFont();
    std::lock_guard<std::mutex> lock(g_font_mutex);
    for (auto &blob : g_fonts) {
        if (blob.data) {
            heap_caps_free(blob.data);
            blob.data = nullptr;
            blob.len = 0;
        }
    }
    g_fonts.clear();
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "push_image_rgb565: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    const int64_t expected_len64 = (int64_t)w * (int64_t)h * 2;
    if (expected_len64 < 0 || expected_len64 > (int64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_rgb565: size overflow");
        return kWasmErrInvalidArgument;
    }
    const size_t expected_len = (size_t)expected_len64;

    if ((!ptr && expected_len != 0) || len != expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_rgb565: ptr/len mismatch");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return kWasmOk;
    }
    if (((uintptr_t)ptr & 1u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_rgb565: ptr must be 2-byte aligned");
        return kWasmErrInvalidArgument;
    }

    display->pushImage(x, y, w, h, (const lgfx::rgb565_t *)ptr);
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "push_image: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    lgfx::color_depth_t depth = lgfx::color_depth_t::rgb565_2Byte;
    uint32_t bits = 0;
    bool requires_palette = false;
    uint32_t palette_entries = 0;
    if (!canonicalize_color_depth(depth_raw, &depth, &bits, &requires_palette, &palette_entries)) {
        return kWasmErrInvalidArgument;
    }

    size_t expected_len = 0;
    if (!compute_expected_image_len(w, h, bits, &expected_len)) {
        return kWasmErrInvalidArgument;
    }

    if ((!data_ptr && expected_len != 0) || data_len != expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: data ptr/len mismatch");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return kWasmOk;
    }

    if (bits == 16 && ((uintptr_t)data_ptr & 1u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: data_ptr must be 2-byte aligned for 16bpp");
        return kWasmErrInvalidArgument;
    }
    if (bits == 32 && ((uintptr_t)data_ptr & 3u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: data_ptr must be 4-byte aligned for 32bpp");
        return kWasmErrInvalidArgument;
    }

    // Palette is only used for indexed (<8bpp) and palette_* modes. For other depths, ignore palette args.
    const lgfx::rgb888_t *palette_rgb888 = nullptr;
    if (requires_palette) {
        if (!palette_ptr) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_ptr is null (palette required)");
            return kWasmErrInvalidArgument;
        }
        if ((palette_len & 3u) != 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_len must be multiple of 4 bytes");
            return kWasmErrInvalidArgument;
        }
        if (((uintptr_t)palette_ptr & 3u) != 0) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_ptr must be 4-byte aligned");
            return kWasmErrInvalidArgument;
        }
        const size_t expected_palette_len = (size_t)palette_entries * 4u;
        if (palette_len != expected_palette_len) {
            wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image: palette_len mismatch");
            return kWasmErrInvalidArgument;
        }

        // Palette entries are passed from WASM as u32 values in 0x00RRGGBB form (little-endian bytes BB GG RR 00),
        // which matches lgfx::rgb888_t memory layout (b, g, r [+ padding]).
        palette_rgb888 = (const lgfx::rgb888_t *)palette_ptr;
    }

    display->pushImage(x, y, w, h, (const void *)data_ptr, depth, palette_rgb888);
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "push_image_gray8: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    const int64_t expected_len64 = (int64_t)w * (int64_t)h;
    if (expected_len64 < 0 || expected_len64 > (int64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_gray8: size overflow");
        return kWasmErrInvalidArgument;
    }
    const size_t expected_len = (size_t)expected_len64;

    if ((!ptr && expected_len != 0) || len != expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "push_image_gray8: ptr/len mismatch");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return kWasmOk;
    }

    const uint32_t white = lgfx::color888(255, 255, 255);
    const uint32_t black = lgfx::color888(0, 0, 0);
    display->pushGrayscaleImage(x, y, w, h, ptr, lgfx::color_depth_t::grayscale_8bit, white, black);
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!validate_display_rect(*display, x, y, w, h, "read_rect_rgb565: rect out of bounds")) {
        return kWasmErrInvalidArgument;
    }

    const int64_t expected_len64 = (int64_t)w * (int64_t)h * 2;
    if (expected_len64 < 0 || expected_len64 > (int64_t)SIZE_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: size overflow");
        return kWasmErrInvalidArgument;
    }
    if (expected_len64 > (int64_t)INT32_MAX) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: output too large");
        return kWasmErrInvalidArgument;
    }

    const size_t expected_len = (size_t)expected_len64;
    if (!out && expected_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: out is null");
        return kWasmErrInvalidArgument;
    }
    if (out_len < expected_len) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: out_len too small");
        return kWasmErrInvalidArgument;
    }
    if (expected_len == 0) {
        return 0;
    }
    if (((uintptr_t)out & 1u) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "read_rect_rgb565: out must be 2-byte aligned");
        return kWasmErrInvalidArgument;
    }

    display->readRect(x, y, w, h, (lgfx::rgb565_t *)out);
    return (int32_t)expected_len;
}

int32_t DisplayLgfx::drawPng(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: negative coordinates");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxPngBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = display->drawPng(ptr, len, x, y);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawXth(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, bool fast)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxXthBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xth: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = lgfx_xtc::drawXth(*display, ptr, len);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_xth: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawXtg(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, bool fast)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        return kWasmOk;
    }
    if (len > kMaxXtgBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_xtg: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = lgfx_xtc::drawXtg(*display, ptr, len);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_xtg: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawJpgFit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y,
    int32_t max_w, int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0 || max_w == 0 || max_h == 0) {
        return kWasmOk;
    }
    if (len > kMaxJpgBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_fit: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = display->drawJpg(ptr, len, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg_fit: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawPngFit(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len, int32_t x, int32_t y,
    int32_t max_w, int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0 || max_w == 0 || max_h == 0) {
        return kWasmOk;
    }
    if (len > kMaxPngBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_fit: len too large");
        return kWasmErrInvalidArgument;
    }

    const bool ok = display->drawPng(ptr, len, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png_fit: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawJpgFile(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_file: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_jpg_file: path is null");
        return kWasmErrInvalidArgument;
    }
    if (max_w == 0 || max_h == 0) {
        return kWasmOk;
    }

    const bool ok = display->drawJpgFile(path, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_jpg_file: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawPngFile(wasm_exec_env_t exec_env, const char *path, int32_t x, int32_t y, int32_t max_w,
    int32_t max_h)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (x < 0 || y < 0 || max_w < 0 || max_h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_file: negative coordinates/size");
        return kWasmErrInvalidArgument;
    }
    if (!path) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_png_file: path is null");
        return kWasmErrInvalidArgument;
    }
    if (max_w == 0 || max_h == 0) {
        return kWasmOk;
    }

    const bool ok = display->drawPngFile(path, x, y, max_w, max_h, 0, 0, 0.0f, 0.0f);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_png_file: decode failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t DisplayLgfx::drawPixel(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->drawPixel(x, y, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::drawLine(wasm_exec_env_t exec_env, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->drawLine(x0, y0, x1, y1, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::drawFastVline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawFastVline: h < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawFastVLine(x, y, h, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::drawFastHline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawFastHline: w < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawFastHLine(x, y, w, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::drawRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawRect: w < 0 or h < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawRect(x, y, w, h, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::fillRect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillRect: w < 0 or h < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillRect(x, y, w, h, color_from_rgb888(rgb888));
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0 || r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawRoundRect: w < 0 or h < 0 or r < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawRoundRect(x, y, w, h, r, color_from_rgb888(rgb888));
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (w < 0 || h < 0 || r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillRoundRect: w < 0 or h < 0 or r < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillRoundRect(x, y, w, h, r, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::drawCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawCircle: r < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawCircle(x, y, r, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::fillCircle(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t r, int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (r < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillCircle: r < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillCircle(x, y, r, color_from_rgb888(rgb888));
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (r0 < 0 || r1 < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillArc: r0 < 0 or r1 < 0");
        return kWasmErrInvalidArgument;
    }
    if (r1 > r0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillArc: r1 > r0");
        return kWasmErrInvalidArgument;
    }
    display->fillArc(x, y, r0, r1, angle0, angle1, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::drawEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry,
    int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rx < 0 || ry < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "drawEllipse: rx < 0 or ry < 0");
        return kWasmErrInvalidArgument;
    }
    display->drawEllipse(x, y, rx, ry, color_from_rgb888(rgb888));
    return kWasmOk;
}

int32_t DisplayLgfx::fillEllipse(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t rx, int32_t ry,
    int32_t rgb888)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (rx < 0 || ry < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "fillEllipse: rx < 0 or ry < 0");
        return kWasmErrInvalidArgument;
    }
    display->fillEllipse(x, y, rx, ry, color_from_rgb888(rgb888));
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->drawTriangle(x0, y0, x1, y1, x2, y2, color_from_rgb888(rgb888));
    return kWasmOk;
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
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->fillTriangle(x0, y0, x1, y1, x2, y2, color_from_rgb888(rgb888));
    return kWasmOk;
}
