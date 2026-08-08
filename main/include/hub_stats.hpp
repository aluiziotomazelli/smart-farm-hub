// main/include/hub_stats.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include "farm_protocol_types.hpp"
#include "protocol_types.hpp"   // espnow::CommandType

static constexpr uint8_t MAX_HUB_NODES = 8;
static constexpr uint8_t MAX_PENDING_PER_NODE = 4;

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

struct NodePersistedMetadata {
    farm::NodeId node_id = farm::NodeId::UNKNOWN;
    farm::PowerProfile power_profile = farm::PowerProfile::DEEP_SLEEP;
    uint8_t fw_major = 0;
    uint8_t fw_minor = 0;
    uint8_t fw_patch = 0;

    bool operator==(const NodePersistedMetadata& other) const {
        return node_id == other.node_id &&
               power_profile == other.power_profile &&
               fw_major == other.fw_major &&
               fw_minor == other.fw_minor &&
               fw_patch == other.fw_patch;
    }
    bool operator!=(const NodePersistedMetadata& other) const {
        return !(*this == other);
    }
};

struct HubStats {
    static constexpr uint32_t MAGIC = 0x485542; // "HUB"
    static constexpr uint8_t VERSION = 5;

    uint32_t magic = MAGIC;
    uint8_t version = VERSION;

    // User preferences
    uint8_t language = 0; // 0: EN_US, 1: PT_BR

    // Lifecycle counters
    uint32_t messages_received  = 0;
    uint32_t commands_sent      = 0;

    // Per-node pending command FIFO queues (survives reboots via NVS)
    PendingNodeCommand pending_cmds[MAX_HUB_NODES][MAX_PENDING_PER_NODE] = {};

    // Per-node remote metadata (power profile & fw version, survives reboots via NVS)
    NodePersistedMetadata node_info[MAX_HUB_NODES] = {};

    // CRC MUST BE LAST of the validated fields
    uint32_t crc = 0;

    void reset() {
        *this = HubStats{};
        magic = MAGIC;
        version = VERSION;
    }

    bool push_pending(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack) {
        if (node_id == farm::NodeId::UNKNOWN) return false;

        int target_row = -1;
        for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
            if (pending_cmds[r][0].active && pending_cmds[r][0].node_id == node_id) {
                target_row = static_cast<int>(r);
                break;
            }
        }
        if (target_row == -1) {
            for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
                if (!pending_cmds[r][0].active) {
                    target_row = static_cast<int>(r);
                    break;
                }
            }
        }

        if (target_row == -1) return false;

        for (size_t c = 0; c < MAX_PENDING_PER_NODE; ++c) {
            if (!pending_cmds[target_row][c].active) {
                pending_cmds[target_row][c] = {true, node_id, cmd, requires_ack};
                return true;
            }
        }
        return false;
    }

    bool pop_pending(farm::NodeId node_id, PendingNodeCommand& out_cmd) {
        for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
            if (pending_cmds[r][0].active && pending_cmds[r][0].node_id == node_id) {
                out_cmd = pending_cmds[r][0];
                for (size_t c = 0; c < MAX_PENDING_PER_NODE - 1; ++c) {
                    pending_cmds[r][c] = pending_cmds[r][c + 1];
                }
                pending_cmds[r][MAX_PENDING_PER_NODE - 1] = {};
                return true;
            }
        }
        return false;
    }

    bool peek_pending(farm::NodeId node_id, PendingNodeCommand& out_cmd) const {
        for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
            if (pending_cmds[r][0].active && pending_cmds[r][0].node_id == node_id) {
                out_cmd = pending_cmds[r][0];
                return true;
            }
        }
        return false;
    }

    bool has_pending(farm::NodeId node_id) const {
        for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
            if (pending_cmds[r][0].active && pending_cmds[r][0].node_id == node_id) {
                return true;
            }
        }
        return false;
    }

    void clear_pending(farm::NodeId node_id) {
        for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
            if (pending_cmds[r][0].active && pending_cmds[r][0].node_id == node_id) {
                for (size_t c = 0; c < MAX_PENDING_PER_NODE; ++c) {
                    pending_cmds[r][c] = {};
                }
                return;
            }
        }
    }

    void set_node_power_profile(farm::NodeId node_id, farm::PowerProfile profile) {
        if (node_id == farm::NodeId::UNKNOWN) return;
        for (size_t i = 0; i < MAX_HUB_NODES; ++i) {
            if (node_info[i].node_id == node_id) {
                node_info[i].power_profile = profile;
                return;
            }
        }
        for (size_t i = 0; i < MAX_HUB_NODES; ++i) {
            if (node_info[i].node_id == farm::NodeId::UNKNOWN) {
                node_info[i].node_id = node_id;
                node_info[i].power_profile = profile;
                return;
            }
        }
    }

    void set_node_fw_version(farm::NodeId node_id, uint8_t major, uint8_t minor, uint8_t patch) {
        if (node_id == farm::NodeId::UNKNOWN) return;
        for (size_t i = 0; i < MAX_HUB_NODES; ++i) {
            if (node_info[i].node_id == node_id) {
                node_info[i].fw_major = major;
                node_info[i].fw_minor = minor;
                node_info[i].fw_patch = patch;
                return;
            }
        }
        for (size_t i = 0; i < MAX_HUB_NODES; ++i) {
            if (node_info[i].node_id == farm::NodeId::UNKNOWN) {
                node_info[i].node_id = node_id;
                node_info[i].fw_major = major;
                node_info[i].fw_minor = minor;
                node_info[i].fw_patch = patch;
                return;
            }
        }
    }

    bool operator==(const HubStats& other) const {
        if (magic != other.magic || version != other.version ||
            language != other.language ||
            messages_received != other.messages_received ||
            commands_sent != other.commands_sent) {
            return false;
        }

        for (size_t r = 0; r < MAX_HUB_NODES; ++r) {
            for (size_t c = 0; c < MAX_PENDING_PER_NODE; ++c) {
                if (pending_cmds[r][c] != other.pending_cmds[r][c]) {
                    return false;
                }
            }
        }

        for (size_t i = 0; i < MAX_HUB_NODES; ++i) {
            if (node_info[i] != other.node_info[i]) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const HubStats& other) const {
        return !(*this == other);
    }
};
