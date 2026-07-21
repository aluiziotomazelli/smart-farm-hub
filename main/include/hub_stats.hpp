// main/include/hub_stats.hpp
#pragma once
#include <cstdint>
#include "farm_protocol_types.hpp"
#include "protocol_types.hpp"   // espnow::CommandType

static constexpr uint8_t MAX_HUB_NODES = 8;

/**
 * @brief Represents a pending command for a specific node.
 * active = false means no pending command for this slot.
 */
struct PendingNodeCommand {
    bool                  active      = false;
    farm::NodeId          node_id     = farm::NodeId::UNKNOWN;
    espnow::CommandType   command     = espnow::CommandType::START_OTA;
};

struct HubStats {
    // Lifecycle
    uint32_t boot_count         = 0;
    uint32_t messages_received  = 0;
    uint32_t commands_sent      = 0;

    // Per-node pending commands (survives reboots via NVS)
    PendingNodeCommand pending_cmds[MAX_HUB_NODES] = {};

    // Last received water tank report (for continuity on reboot)
    uint16_t last_wt_level_permille = 0;
    float    last_wt_distance_cm    = 0.0f;
    uint16_t last_wt_battery_mv     = 0;

    void reset() { *this = HubStats(); }
};
