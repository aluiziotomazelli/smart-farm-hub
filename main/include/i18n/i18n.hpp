#pragma once
#include "language.hpp"
#include "strings_en.hpp"
#include "strings_pt.hpp"

/**
 * @class I18n
 * @brief Static internationalization (i18n) manager for runtime language selection and string resolution.
 *
 * Provides thread-safe, static string lookups from Flash-backed string tables without dynamic RAM allocations.
 */
class I18n {
public:
    /**
     * @brief Set the active active user interface language.
     *
     * @param lang Target language to activate.
     */
    static void set_language(Language lang) {
        current_lang_ = lang;
    }

    /**
     * @brief Get the currently active user interface language.
     *
     * @return Currently selected Language enum value.
     */
    static Language get_language() {
        return current_lang_;
    }

    /**
     * @brief Retrieve the localized string for a given string identifier.
     *
     * @param id The string identifier (StrId).
     * @return Pointer to null-terminated C-string stored in Flash memory.
     *
     * @note Fallbacks to EN_US strings if an unsupported language value is active.
     */
    static const char* get(StrId id) {
        size_t idx = static_cast<size_t>(id);
        switch (current_lang_) {
        case Language::PT_BR:
            return STRINGS_PT[idx];
        default:
            return STRINGS_EN[idx];
        }
    }

private:
    inline static Language current_lang_ = Language::EN_US;
};

