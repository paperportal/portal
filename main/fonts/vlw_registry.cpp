#include "fonts/vlw_registry.h"

#include <limits>

#include "wasm/api/display.h"

namespace {

extern const uint8_t _binary_inter_medium_32_vlw_start[] asm("_binary_inter_medium_32_vlw_start");
extern const uint8_t _binary_inter_medium_32_vlw_end[] asm("_binary_inter_medium_32_vlw_end");
extern const uint8_t _binary_montserrat_light_20_vlw_start[] asm("_binary_montserrat_light_20_vlw_start");
extern const uint8_t _binary_montserrat_light_20_vlw_end[] asm("_binary_montserrat_light_20_vlw_end");

/** @brief Memoized state for one embedded firmware VLW font. */
struct SystemFontSlot {
    std::once_flag once;
    std::shared_ptr<VlwFont> font;
    std::string error;
};

/** @brief Cache of lazily parsed embedded VLW system fonts keyed by API font id. */
SystemFontSlot g_system_fonts[2];

} // namespace

/** @brief Parse a VLW payload, store it, and return a stable positive handle. */
int32_t VlwRegistry::RegisterCopy(const uint8_t *ptr, size_t len, const char *debug_name, std::string *out_error)
{
    std::string parse_error;
    std::shared_ptr<VlwFont> font = VlwFont::CreateCopy(ptr, len, debug_name, &parse_error);
    if (!font) {
        if (out_error) {
            *out_error = parse_error;
        }
        return -1;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (next_handle_ <= 0 || next_handle_ == std::numeric_limits<int32_t>::max()) {
        if (out_error) {
            *out_error = "VLW handle counter exhausted";
        }
        return -1;
    }

    const int32_t handle = next_handle_++;
    fonts_[handle] = std::move(font);
    return handle;
}

/** @brief Look up a registered app-owned VLW font handle. */
std::shared_ptr<VlwFont> VlwRegistry::Get(int32_t handle) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fonts_.find(handle);
    if (it == fonts_.end()) {
        return nullptr;
    }
    return it->second;
}

/** @brief Drop one registered font handle from the registry. */
bool VlwRegistry::Remove(int32_t handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fonts_.erase(handle) != 0;
}

/** @brief Drop every app-owned VLW font from the registry. */
void VlwRegistry::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    fonts_.clear();
}

/** @brief Lazily parse and return one of the embedded system VLW fonts. */
std::shared_ptr<VlwFont> GetSystemVlwFont(int32_t font_id, std::string *out_error)
{
    if (font_id < kVlwSystemFontInter || font_id > kVlwSystemFontMontserrat) {
        if (out_error) {
            *out_error = "invalid system VLW font id";
        }
        return nullptr;
    }

    SystemFontSlot &slot = g_system_fonts[font_id];
    std::call_once(slot.once, [&slot, font_id]() {
        const uint8_t *font_ptr = nullptr;
        size_t font_len = 0;
        const char *font_name = GetSystemVlwFontName(font_id);
        switch (font_id) {
        case kVlwSystemFontInter:
            font_ptr = _binary_inter_medium_32_vlw_start;
            font_len = (size_t)(_binary_inter_medium_32_vlw_end - _binary_inter_medium_32_vlw_start);
            break;
        case kVlwSystemFontMontserrat:
            font_ptr = _binary_montserrat_light_20_vlw_start;
            font_len = (size_t)(_binary_montserrat_light_20_vlw_end - _binary_montserrat_light_20_vlw_start);
            break;
        }

        slot.font = VlwFont::CreateCopy(font_ptr, font_len, font_name, &slot.error);
        if (!slot.font && slot.error.empty()) {
            slot.error = "failed to parse embedded VLW font";
        }
    });

    if (!slot.font && out_error) {
        *out_error = slot.error;
    }
    return slot.font;
}

/** @brief Map a public system font id to its embedded asset name. */
const char *GetSystemVlwFontName(int32_t font_id)
{
    switch (font_id) {
    case kVlwSystemFontInter:
        return "inter_medium_32";
    case kVlwSystemFontMontserrat:
        return "montserrat_light_20";
    default:
        return "unknown";
    }
}
