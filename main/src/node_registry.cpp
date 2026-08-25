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

    // 2. Allocate in first free slot
    for (auto& entry : nodes_) {
        if (entry.node_id == farm::NodeId::UNKNOWN) {
            entry.node_id = node_id;
            entry.power_profile = profile;
            entry.fw_major = major;
            entry.fw_minor = minor;
            entry.fw_patch = patch;
            return;
        }
    }
}

void NodeRegistry::set_power_profile(farm::NodeId node_id, farm::PowerProfile profile)
{
    if (node_id == farm::NodeId::UNKNOWN || node_id == farm::NodeId::BROADCAST) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            entry.power_profile = profile;
            return;
        }
    }

    // If not registered yet, create default entry with this profile
    for (auto& entry : nodes_) {
        if (entry.node_id == farm::NodeId::UNKNOWN) {
            entry.node_id = node_id;
            entry.power_profile = profile;
            return;
        }
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

    etl::vector<farm::NodeMetadata, farm::MAX_HUB_NODES> result;
    for (const auto& entry : nodes_) {
        if (entry.node_id != farm::NodeId::UNKNOWN) {
            result.push_back(entry);
        }
    }

    return result;
}

bool NodeRegistry::remove_node(farm::NodeId node_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& entry : nodes_) {
        if (entry.node_id == node_id) {
            entry = farm::NodeMetadata{};
            return true;
        }
    }

    return false;
}

void NodeRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : nodes_) {
        entry = farm::NodeMetadata{};
    }
}

} // namespace hub
