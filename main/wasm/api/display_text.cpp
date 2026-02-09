#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <mutex>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "wasm_export.h"

#include "m5papers3_display.h"

#include "../api.h"
#include "errors.h"

namespace {

extern const uint8_t _binary_inter_medium_32_vlw_start[] asm("_binary_inter_medium_32_vlw_start");
extern const uint8_t _binary_inter_medium_32_vlw_end[] asm("_binary_inter_medium_32_vlw_end");
extern const uint8_t _binary_montserrat_light_20_vlw_start[] asm("_binary_montserrat_light_20_vlw_start");
extern const uint8_t _binary_montserrat_light_20_vlw_end[] asm("_binary_montserrat_light_20_vlw_end");

constexpr const char *kTag = "wasm_api_display_text";
constexpr size_t kMaxVlwBytes = 1024 * 1024;
constexpr int32_t kVlwSystemFontInter = 0;
constexpr int32_t kVlwSystemFontMontserrat = 1;

struct FontBlob {
    uint8_t *data = nullptr;
    size_t len = 0;
};

std::mutex g_font_mutex;
std::vector<FontBlob> g_fonts;

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

int32_t set_cursor(wasm_exec_env_t exec_env, int32_t x, int32_t y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setCursor(x, y);
    return kWasmOk;
}

int32_t set_text_size(wasm_exec_env_t exec_env, float sx, float sy)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!(sx > 0.0f) || !(sy > 0.0f)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "set_text_size: sx/sy must be > 0");
        return kWasmErrInvalidArgument;
    }
    display->setTextSize(sx, sy);
    return kWasmOk;
}

int32_t set_text_datum(wasm_exec_env_t exec_env, int32_t datum)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!is_valid_text_datum(datum)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "set_text_datum: invalid datum");
        return kWasmErrInvalidArgument;
    }
    display->setTextDatum((uint8_t)datum);
    return kWasmOk;
}

int32_t set_text_color(wasm_exec_env_t exec_env, int32_t fg_rgb888, int32_t bg_rgb888, int32_t use_bg)
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

int32_t set_text_wrap(wasm_exec_env_t exec_env, int32_t wrap_x, int32_t wrap_y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setTextWrap(wrap_x != 0, wrap_y != 0);
    return kWasmOk;
}

int32_t set_text_scroll(wasm_exec_env_t exec_env, int32_t scroll)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->setTextScroll(scroll != 0);
    return kWasmOk;
}

int32_t set_text_font(wasm_exec_env_t exec_env, int32_t font_id)
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
        wasm_api_set_last_error(kWasmErrInvalidArgument, "set_text_font: unknown font_id");
        return kWasmErrInvalidArgument;
    }
    return kWasmOk;
}

int32_t draw_string(wasm_exec_env_t exec_env, const char *s, int32_t x, int32_t y)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!s) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "draw_string: s is null");
        return kWasmErrInvalidArgument;
    }
    const size_t w = display->drawString(s, x, y);
    if (w > (size_t)INT32_MAX) {
        wasm_api_set_last_error(kWasmErrInternal, "draw_string: width overflow");
        return kWasmErrInternal;
    }
    return (int32_t)w;
}

int32_t text_width(wasm_exec_env_t exec_env, const char *s)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    if (!s) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "text_width: s is null");
        return kWasmErrInvalidArgument;
    }

    const int32_t w = (int32_t)display->textWidth(s);
    if (w < 0) {
        wasm_api_set_last_error(kWasmErrInternal, "text_width: negative width");
        return kWasmErrInternal;
    }
    return w;
}

int32_t font_height(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    return (int32_t)display->fontHeight();
}

int32_t set_text_encoding(wasm_exec_env_t exec_env, int32_t utf8_enable, int32_t cp437_enable)
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

int32_t vlw_register(wasm_exec_env_t exec_env, const uint8_t *ptr, size_t len)
{
    (void)exec_env;
    if (!ptr && len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlw_register: ptr is null");
        return kWasmErrInvalidArgument;
    }
    if (len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlw_register: len is 0");
        return kWasmErrInvalidArgument;
    }
    if (len > kMaxVlwBytes) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlw_register: len too large");
        return kWasmErrInvalidArgument;
    }

    uint8_t *copy = alloc_font_bytes(len);
    if (!copy) {
        wasm_api_set_last_error(kWasmErrInternal, "vlw_register: alloc failed");
        return kWasmErrInternal;
    }
    memcpy(copy, ptr, len);

    std::lock_guard<std::mutex> lock(g_font_mutex);
    const int32_t handle = (int32_t)g_fonts.size();
    g_fonts.push_back(FontBlob{copy, len});
    return handle;
}

int32_t vlw_use(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    std::lock_guard<std::mutex> lock(g_font_mutex);
    if (handle < 0 || (size_t)handle >= g_fonts.size()) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlw_use: invalid handle");
        return kWasmErrInvalidArgument;
    }
    FontBlob &blob = g_fonts[(size_t)handle];
    if (!blob.data || blob.len == 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlw_use: font empty");
        return kWasmErrInvalidArgument;
    }
    display->unloadFont();
    bool ok = display->loadFont(blob.data);
    if (!ok) {
        wasm_api_set_last_error(kWasmErrInternal, "vlw_use: loadFont failed");
        return kWasmErrInternal;
    }
    return kWasmOk;
}

int32_t vlw_use_system(wasm_exec_env_t exec_env, int32_t font_id)
{
    (void)exec_env;
    ESP_LOGI(kTag, "vlw_use_system called (font_id=%" PRId32 ")", font_id);

    auto *display = get_display_or_set_error();
    if (!display) {
        ESP_LOGI(kTag, "vlw_use_system aborted: display not ready");
        return kWasmErrNotReady;
    }

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
        ESP_LOGI(kTag, "vlw_use_system rejected invalid font_id=%" PRId32, font_id);
        wasm_api_set_last_error(kWasmErrInvalidArgument, "vlw_use_system: invalid font_id");
        return kWasmErrInvalidArgument;
    }

    ESP_LOGI(kTag, "vlw_use_system loading font '%s' (%u bytes)", font_name, (unsigned)font_bytes);
    display->unloadFont();
    bool ok = display->loadFont(font_ptr);
    if (!ok) {
        ESP_LOGI(kTag, "vlw_use_system failed to load font '%s' (font_id=%" PRId32 ")", font_name, font_id);
        wasm_api_set_last_error(kWasmErrInternal, "vlw_use_system: loadFont failed");
        return kWasmErrInternal;
    }
    ESP_LOGI(kTag, "vlw_use_system loaded font '%s' (font_id=%" PRId32 ")", font_name, font_id);
    return kWasmOk;
}

int32_t vlw_unload(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    auto *display = get_display_or_set_error();
    if (!display) {
        return kWasmErrNotReady;
    }
    display->unloadFont();
    return kWasmOk;
}

int32_t vlw_clear_all(wasm_exec_env_t exec_env)
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

/* clang-format off */
#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, (void *)func_name, signature, NULL }

static NativeSymbol g_display_text_native_symbols[] = {
    REG_NATIVE_FUNC(set_cursor, "(ii)i"),
    REG_NATIVE_FUNC(set_text_size, "(ff)i"),
    REG_NATIVE_FUNC(set_text_datum, "(i)i"),
    REG_NATIVE_FUNC(set_text_color, "(iii)i"),
    REG_NATIVE_FUNC(set_text_wrap, "(ii)i"),
    REG_NATIVE_FUNC(set_text_scroll, "(i)i"),
    REG_NATIVE_FUNC(set_text_font, "(i)i"),
    REG_NATIVE_FUNC(set_text_encoding, "(ii)i"),
    REG_NATIVE_FUNC(draw_string, "(*ii)i"),
    REG_NATIVE_FUNC(text_width, "(*)i"),
    REG_NATIVE_FUNC(font_height, "()i"),
    REG_NATIVE_FUNC(vlw_register, "(*~)i"),
    REG_NATIVE_FUNC(vlw_use, "(i)i"),
    REG_NATIVE_FUNC(vlw_use_system, "(i)i"),
    REG_NATIVE_FUNC(vlw_unload, "()i"),
    REG_NATIVE_FUNC(vlw_clear_all, "()i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_display_text(void)
{
    const uint32_t count = sizeof(g_display_text_native_symbols) / sizeof(g_display_text_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_display", g_display_text_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_display text natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_display_text: wasm_runtime_register_natives failed");
    }
    return ok;
}
