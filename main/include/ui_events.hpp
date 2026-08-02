// main/include/ui_events.hpp
#pragma once

#include <cstdint>

enum class UiEventType {
    NAV_NEXT,    ///< Navigate next (Encoder CW)
    NAV_PREV,    ///< Navigate prev (Encoder CCW)
    CONFIRM,     ///< Select/Confirm (Encoder push click)
    BACK,        ///< Back/Cancel (Encoder push long click)
    BOOT_CLICK   ///< Boot button click
};

struct UiEvent {
    UiEventType type;
    int32_t value{0};
};
