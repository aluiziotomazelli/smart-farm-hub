#include "ui_controller.hpp"
#include "esp_timer.h" // For calculating elapsed time
#include <stdio.h>

UIController::UIController(IGraphicsContext& gfx) : gfx_(gfx) {}

void UIController::render_main_screen(const SystemState& state) {
    char buf[64];

    // --- Header ---
    // [W] FARM HUB [B] [O]
    gfx_.draw_string(0, 0, state.wifi_connected ? "[W]" : "[_]", 1);
    gfx_.draw_string(24, 0, "FARM HUB", 1);
    // Placeholder for Battery and OTA (could parse state if available)
    gfx_.draw_string(90, 0, "[B]", 1);
    gfx_.draw_string(114, 0, state.ota_in_progress ? "[O]" : "[_]", 1);
    
    // Separator
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Row 1: Level + Timestamp + Distance ---
    uint32_t now = esp_timer_get_time();
    uint32_t elapsed_s = 0;
    if (state.last_water_update_ts > 0 && now >= state.last_water_update_ts) {
        elapsed_s = (now - state.last_water_update_ts) / 1000000;
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;

    float level_percent = state.water_level_permille / 10.0f;
    snprintf(buf, sizeof(buf), "  %.1f  %lu:%02lu   %.1f", level_percent, mm, ss, state.water_distance_cm);
    gfx_.draw_string(0, 12, buf, 1);

    // --- Row 2: Visual bar ---
    // Draw an empty box
    gfx_.draw_rect(0, 22, 128, 8, 1);
    // Fill the percentage
    int fill_width = (state.water_level_permille * 126) / 1000;
    for (int i = 0; i < fill_width; ++i) {
        gfx_.draw_vline(1 + i, 23, 6, 1);
    }

    // --- Row 3: WiFi Status ---
    snprintf(buf, sizeof(buf), "WiFi: %-3s    R: %d", state.wifi_connected ? "ON" : "OFF", state.wifi_rssi);
    gfx_.draw_string(0, 34, buf, 1);

    // --- Row 4: ESP-NOW Status ---
    snprintf(buf, sizeof(buf), "E-NOW  Pr: %d  R: %d", state.espnow_peers, state.espnow_avg_rssi);
    gfx_.draw_string(0, 44, buf, 1);

    // --- Row 5: Stats ---
    snprintf(buf, sizeof(buf), "S: %-4lu  L: %-2lu  T:%-2lu", state.messages_sent, state.messages_lost, state.last_rtt_ms);
    gfx_.draw_string(0, 54, buf, 1);
}

void UIController::render_stats_screen(const SystemState& state) {
    gfx_.draw_string(0, 0, "STATS SCREEN", 1);
    // Placeholder for actual stats
}

void UIController::render_boot_screen() {
    gfx_.draw_string(30, 28, "STARTING...", 1);
}
