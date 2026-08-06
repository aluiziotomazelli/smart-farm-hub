// main/include/hub_stats.hpp
#pragma once
#include <cstdint>
#include <cstddef>
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
    bool                  requires_ack = true;

    bool operator==(const PendingNodeCommand& other) const {
        return active == other.active && node_id == other.node_id && command == other.command && requires_ack == other.requires_ack;
    }
    bool operator!=(const PendingNodeCommand& other) const {
        return !(*this == other);
    }
};

struct HubStats {
    static constexpr uint32_t MAGIC = 0x485542; // "HUB"
    static constexpr uint8_t VERSION = 2;

    uint32_t magic = MAGIC;
    uint8_t version = VERSION;

    // User preferences
    uint8_t language = 0; // 0: EN_US, 1: PT_BR

    // Lifecycle counters
    uint32_t messages_received  = 0;
    uint32_t commands_sent      = 0;

    // Per-node pending commands (survives reboots via NVS)
    PendingNodeCommand pending_cmds[MAX_HUB_NODES] = {};

    // Last received water tank report (for continuity on reboot)
    uint16_t last_wt_level_permille = 0;
    float    last_wt_distance_cm    = 0.0f;
    uint16_t last_wt_battery_mv     = 0;

    // CRC MUST BE LAST of the validated fields
    uint32_t crc = 0;

    void reset() {
        *this = HubStats{};
        magic = MAGIC;
        version = VERSION;
    }

    bool operator==(const HubStats& other) const {
        if (magic != other.magic || version != other.version ||
            language != other.language ||
            messages_received != other.messages_received ||
            commands_sent != other.commands_sent ||
            last_wt_level_permille != other.last_wt_level_permille ||
            last_wt_distance_cm != other.last_wt_distance_cm ||
            last_wt_battery_mv != other.last_wt_battery_mv) {
            return false;
        }

        for (size_t i = 0; i < MAX_HUB_NODES; ++i) {
            if (pending_cmds[i] != other.pending_cmds[i]) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const HubStats& other) const {
        return !(*this == other);
    }
};
