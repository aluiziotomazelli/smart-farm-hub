// host_test/test_hub/main/test_node_registry.cpp
#include <gtest/gtest.h>
#include "node_registry.hpp"

using namespace hub;

TEST(NodeRegistryTest, DefaultValuesReturnDeepSleepForUnknownNode)
{
    NodeRegistry registry;
    EXPECT_EQ(registry.get_power_profile(farm::NodeId::PUMP_CONTROL), farm::PowerProfile::DEEP_SLEEP);
    EXPECT_EQ(registry.get_power_profile(farm::NodeId::WATER_TANK), farm::PowerProfile::DEEP_SLEEP);
    EXPECT_TRUE(registry.get_all_nodes().empty());
}

TEST(NodeRegistryTest, RegisterAndRetrieveNodeMetadata)
{
    NodeRegistry registry;
    registry.set_node_metadata(farm::NodeId::PUMP_CONTROL, farm::PowerProfile::ALWAYS_ON, 1, 2, 3);
    registry.set_node_metadata(farm::NodeId::WATER_TANK, farm::PowerProfile::DEEP_SLEEP, 2, 0, 1);

    EXPECT_EQ(registry.get_power_profile(farm::NodeId::PUMP_CONTROL), farm::PowerProfile::ALWAYS_ON);
    EXPECT_EQ(registry.get_power_profile(farm::NodeId::WATER_TANK), farm::PowerProfile::DEEP_SLEEP);

    farm::NodeMetadata entry{};
    EXPECT_TRUE(registry.get_node_info(farm::NodeId::PUMP_CONTROL, entry));
    EXPECT_EQ(entry.node_id, farm::NodeId::PUMP_CONTROL);
    EXPECT_EQ(entry.power_profile, farm::PowerProfile::ALWAYS_ON);
    EXPECT_EQ(entry.fw_major, 1);
    EXPECT_EQ(entry.fw_minor, 2);
    EXPECT_EQ(entry.fw_patch, 3);

    auto all_nodes = registry.get_all_nodes();
    ASSERT_EQ(all_nodes.size(), 2);
    EXPECT_EQ(all_nodes[0].node_id, farm::NodeId::PUMP_CONTROL);
    EXPECT_EQ(all_nodes[1].node_id, farm::NodeId::WATER_TANK);
}

TEST(NodeRegistryTest, UpdateExistingNodeMetadataAndPowerProfile)
{
    NodeRegistry registry;
    registry.set_node_metadata(farm::NodeId::SOLAR_SENSOR, farm::PowerProfile::ALWAYS_ON, 1, 0, 0);
    EXPECT_EQ(registry.get_power_profile(farm::NodeId::SOLAR_SENSOR), farm::PowerProfile::ALWAYS_ON);

    // Transition to night mode: update power profile only
    registry.set_power_profile(farm::NodeId::SOLAR_SENSOR, farm::PowerProfile::DEEP_SLEEP);
    EXPECT_EQ(registry.get_power_profile(farm::NodeId::SOLAR_SENSOR), farm::PowerProfile::DEEP_SLEEP);

    // Update FW version
    registry.set_node_metadata(farm::NodeId::SOLAR_SENSOR, farm::PowerProfile::DEEP_SLEEP, 1, 1, 0);
    farm::NodeMetadata entry{};
    EXPECT_TRUE(registry.get_node_info(farm::NodeId::SOLAR_SENSOR, entry));
    EXPECT_EQ(entry.fw_minor, 1);
    EXPECT_EQ(registry.get_all_nodes().size(), 1);
}

TEST(NodeRegistryTest, UpdateFwVersionOnly)
{
    NodeRegistry registry;
    registry.set_node_metadata(farm::NodeId::PUMP_CONTROL, farm::PowerProfile::ALWAYS_ON, 1, 0, 0);

    // Update FW version via dedicated method
    registry.set_fw_version(farm::NodeId::PUMP_CONTROL, 2, 1, 4);

    farm::NodeMetadata entry{};
    EXPECT_TRUE(registry.get_node_info(farm::NodeId::PUMP_CONTROL, entry));
    EXPECT_EQ(entry.power_profile, farm::PowerProfile::ALWAYS_ON);
    EXPECT_EQ(entry.fw_major, 2);
    EXPECT_EQ(entry.fw_minor, 1);
    EXPECT_EQ(entry.fw_patch, 4);

    // Calling on new node sets version with default DEEP_SLEEP profile
    registry.set_fw_version(farm::NodeId::SOLAR_SENSOR, 1, 0, 5);
    EXPECT_TRUE(registry.get_node_info(farm::NodeId::SOLAR_SENSOR, entry));
    EXPECT_EQ(entry.fw_patch, 5);
    EXPECT_EQ(entry.power_profile, farm::PowerProfile::DEEP_SLEEP);
}

TEST(NodeRegistryTest, RemoveAndClearNodes)
{
    NodeRegistry registry;
    registry.set_node_metadata(farm::NodeId::PUMP_CONTROL, farm::PowerProfile::ALWAYS_ON, 1, 0, 0);
    registry.set_node_metadata(farm::NodeId::WATER_TANK, farm::PowerProfile::DEEP_SLEEP, 1, 0, 0);

    EXPECT_EQ(registry.get_all_nodes().size(), 2);

    EXPECT_TRUE(registry.remove_node(farm::NodeId::PUMP_CONTROL));
    EXPECT_FALSE(registry.remove_node(farm::NodeId::PUMP_CONTROL)); // Already removed
    EXPECT_EQ(registry.get_all_nodes().size(), 1);
    EXPECT_EQ(registry.get_power_profile(farm::NodeId::PUMP_CONTROL), farm::PowerProfile::DEEP_SLEEP);

    registry.clear();
    EXPECT_TRUE(registry.get_all_nodes().empty());
}
