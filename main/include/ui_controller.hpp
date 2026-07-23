#pragma once

#include "i_graphics_context.hpp"
#include "system_state.hpp"

enum class ScreenMode { MAIN_SCREEN, STATS_SCREEN, BOOT_SCREEN, WATER_TANK_SCREEN };

class UIController {
public:
    explicit UIController(IGraphicsContext& gfx);
    void render_main_screen(const SystemState& state);
    void render_stats_screen(const SystemState& state);
    void render_boot_screen();
    void render_water_tank_screen(const SystemState& state);

private:
    IGraphicsContext& gfx_;
};
