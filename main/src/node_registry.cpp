// main/src/node_registry.cpp
#include "node_registry.hpp"

namespace hub {

void NodeRegistry::set_node_metadata(
    farm::NodeId node_id,
    farm::PowerProfile profile,
    uint8_t major,
    uint8_t minor,
    uint8_t patch)
{
    if (node_id == farm::NodeId::UNKNOWN || node_id == farm::NodeId::BROADCAST) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Update existing entry if present
    for (auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            entry.power_profile = profile;
            entry.fw_major = major;
            entry.fw_minor = minor;
            entry.fw_patch = patch;
            return;
        }
    }

    // 2. Append new node if capacity allows
    if (!nodes_.full()) {
        farm::NodeMetadata meta{};
        meta.node_id = node_id;
        meta.power_profile = profile;
        meta.fw_major = major;
        meta.fw_minor = minor;
        meta.fw_patch = patch;
        nodes_.push_back(meta);
    }
}

void NodeRegistry::set_power_profile(farm::NodeId node_id, farm::PowerProfile profile)
{
    if (node_id == farm::NodeId::UNKNOWN || node_id == farm::NodeId::BROADCAST) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. If entry exists, return early if unchanged, or update
    for (auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            if (entry.power_profile == profile) {
                return; // Early return: zero change
            }
            entry.power_profile = profile;
            return;
        }
    }

    // 2. If not registered yet, append new node with default metadata
    if (!nodes_.full()) {
        farm::NodeMetadata meta{};
        meta.node_id = node_id;
        meta.power_profile = profile;
        nodes_.push_back(meta);
    }
}

void NodeRegistry::set_fw_version(
    farm::NodeId node_id,
    uint8_t major,
    uint8_t minor,
    uint8_t patch)
{
    if (node_id == farm::NodeId::UNKNOWN || node_id == farm::NodeId::BROADCAST) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. If entry exists, return early if unchanged, or update
    for (auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            if (entry.fw_major == major && entry.fw_minor == minor && entry.fw_patch == patch) {
                return; // Early return: zero change
            }
            entry.fw_major = major;
            entry.fw_minor = minor;
            entry.fw_patch = patch;
            return;
        }
    }

    // 2. If not registered yet, append new node with default profile
    if (!nodes_.full()) {
        farm::NodeMetadata meta{};
        meta.node_id = node_id;
        meta.fw_major = major;
        meta.fw_minor = minor;
        meta.fw_patch = patch;
        nodes_.push_back(meta);
    }
}

farm::PowerProfile NodeRegistry::get_power_profile(farm::NodeId node_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            return entry.power_profile;
        }
    }

    // Safe fallback for unregistered battery nodes
    return farm::PowerProfile::DEEP_SLEEP;
}

bool NodeRegistry::get_node_info(farm::NodeId node_id, farm::NodeMetadata& out_entry) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            out_entry = entry;
            return true;
        }
    }

    return false;
}

etl::vector<farm::NodeMetadata, farm::MAX_HUB_NODES> NodeRegistry::get_all_nodes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_;
}

bool NodeRegistry::remove_node(farm::NodeId node_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it->node_id == node_id) {
            nodes_.erase(it);
            return true;
        }
    }

    return false;
}

void NodeRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.clear();
}

} // namespace hub
