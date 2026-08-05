#pragma once
#include "language.hpp"
#include "strings_en.hpp"
#include "strings_pt.hpp"

class I18n {
public:
    static void set_language(Language lang) {
        current_lang_ = lang;
    }

    static Language get_language() {
        return current_lang_;
    }

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
