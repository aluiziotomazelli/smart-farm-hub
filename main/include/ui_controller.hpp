#pragma once

#include "i_graphics_context.hpp"
#include "system_state.hpp"

enum class ScreenMode { MAIN_SCREEN, STATS_SCREEN, BOOT_SCREEN };

class UIController {
public:
    explicit UIController(IGraphicsContext& gfx);
    void render_main_screen(const SystemState& state);
    void render_stats_screen(const SystemState& state);
    void render_boot_screen();

private:
    IGraphicsContext& gfx_;
};
