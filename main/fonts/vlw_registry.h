#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "fonts/vlw_font.h"

/** @brief Stores app-owned VLW fonts and returns stable integer handles for FastEPD. */
class VlwRegistry {
public:
    /**
     * @brief Parse and store a private copy of a VLW font payload.
     * @param ptr Source VLW bytes.
     * @param len Length of @p ptr in bytes.
     * @param debug_name Human-readable name used in logs and errors.
     * @param out_error Optional parse error output.
     * @return Positive font handle on success, otherwise `-1`.
     */
    int32_t RegisterCopy(const uint8_t *ptr, size_t len, const char *debug_name, std::string *out_error);
    /** @brief Look up a previously registered font handle. */
    std::shared_ptr<VlwFont> Get(int32_t handle) const;
    /** @brief Remove one registered font handle. */
    bool Remove(int32_t handle);
    /** @brief Remove every registered app-owned VLW font. */
    void Clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<int32_t, std::shared_ptr<VlwFont>> fonts_;
    int32_t next_handle_ = 1;
};

/**
 * @brief Return one of the firmware-embedded VLW system fonts.
 * @param font_id Public system font identifier from the display API.
 * @param out_error Optional error output when the font id is invalid or parsing failed.
 * @return Shared parsed font, or `nullptr` on failure.
 */
std::shared_ptr<VlwFont> GetSystemVlwFont(int32_t font_id, std::string *out_error = nullptr);
/** @brief Return the diagnostic name for an embedded system VLW font id. */
const char *GetSystemVlwFontName(int32_t font_id);
