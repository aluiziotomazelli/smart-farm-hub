// main/include/node_registry.hpp
#pragma once

#include <array>
#include <mutex>
#include "interfaces/i_node_registry.hpp"

namespace hub {

/**
 * @class NodeRegistry
 * @brief Thread-safe concrete implementation of INodeRegistry using fixed storage and std::mutex.
 */
class NodeRegistry : public INodeRegistry {
public:
    NodeRegistry() = default;

    /** @copydoc INodeRegistry::set_node_metadata */
    void set_node_metadata(
        farm::NodeId node_id,
        farm::PowerProfile profile,
        uint8_t major,
        uint8_t minor,
        uint8_t patch) override;

    /** @copydoc INodeRegistry::set_power_profile */
    void set_power_profile(farm::NodeId node_id, farm::PowerProfile profile) override;

    /** @copydoc INodeRegistry::set_fw_version */
    void set_fw_version(
        farm::NodeId node_id,
        uint8_t major,
        uint8_t minor,
        uint8_t patch) override;

    /** @copydoc INodeRegistry::get_power_profile */
    farm::PowerProfile get_power_profile(farm::NodeId node_id) const override;

    /** @copydoc INodeRegistry::get_node_info */
    bool get_node_info(farm::NodeId node_id, farm::NodeMetadata& out_entry) const override;

    /** @copydoc INodeRegistry::get_all_nodes */
    etl::vector<farm::NodeMetadata, farm::MAX_HUB_NODES> get_all_nodes() const override;

    /** @copydoc INodeRegistry::remove_node */
    bool remove_node(farm::NodeId node_id) override;

    /** @copydoc INodeRegistry::clear */
    void clear() override;

private:
    mutable std::mutex mutex_;
    std::array<farm::NodeMetadata, farm::MAX_HUB_NODES> nodes_{};
};

} // namespace hub
