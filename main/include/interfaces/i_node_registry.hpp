// main/include/interfaces/i_node_registry.hpp
#pragma once

#include <cstdint>
#include "etl/vector.h"
#include "farm_protocol_types.hpp"

namespace hub {

/**
 * @interface INodeRegistry
 * @brief Thread-safe registry maintaining identity, power profile, and firmware version of nodes.
 */
class INodeRegistry {
public:
    virtual ~INodeRegistry() = default;

    /**
     * @brief Registers or updates metadata for a node.
     * @param node_id Target node identifier.
     * @param profile Power profile (ALWAYS_ON, LOW_POWER, DEEP_SLEEP).
     * @param major Firmware major version.
     * @param minor Firmware minor version.
     * @param patch Firmware patch version.
     */
    virtual void set_node_metadata(
        farm::NodeId node_id,
        farm::PowerProfile profile,
        uint8_t major,
        uint8_t minor,
        uint8_t patch) = 0;

    /**
     * @brief Updates only the power profile of a node (e.g. day/night transitions).
     * @param node_id Target node identifier.
     * @param profile New power profile.
     */
    virtual void set_power_profile(farm::NodeId node_id, farm::PowerProfile profile) = 0;

    /**
     * @brief Queries the power profile of a node.
     * @param node_id Target node identifier.
     * @return PowerProfile of the node (DEEP_SLEEP if unknown/unregistered).
     */
    virtual farm::PowerProfile get_power_profile(farm::NodeId node_id) const = 0;

    /**
     * @brief Retrieves detailed metadata of a specific node.
     * @param node_id Target node identifier.
     * @param out_entry Output parameter populated if found.
     * @return true if node is found in registry, false otherwise.
     */
    virtual bool get_node_info(farm::NodeId node_id, farm::NodeMetadata& out_entry) const = 0;

    /**
     * @brief Retrieves all registered nodes (Zero heap allocation).
     * @return etl::vector containing only active registered nodes.
     */
    virtual etl::vector<farm::NodeMetadata, farm::MAX_HUB_NODES> get_all_nodes() const = 0;

    /**
     * @brief Removes a node from the registry.
     * @param node_id Target node identifier.
     * @return true if node was removed, false if not found.
     */
    virtual bool remove_node(farm::NodeId node_id) = 0;

    /**
     * @brief Clears all registered nodes.
     */
    virtual void clear() = 0;
};

} // namespace hub
