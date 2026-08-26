// host_test_common/mock_command_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_command_manager.hpp"

namespace hub {

class MockCommandManager : public ICommandManager {
public:
    MOCK_METHOD(bool, send_command, (farm::NodeId target_node, espnow::CommandType cmd, bool requires_ack), (override));
    MOCK_METHOD(void, process_node_wake, (farm::NodeId node_id, uint64_t node_unix_time_ms), (override));
    MOCK_METHOD(esp_err_t, broadcast_tank_level, (uint8_t tank_id, uint16_t level_permille, bool backup_mode_active, bool float_switch_is_full), (override));
    MOCK_METHOD(bool, dispatch_decision, (const LoadControlDecision& decision), (override));
    MOCK_METHOD(uint32_t, get_messages_received, (), (const, override));
    MOCK_METHOD(uint32_t, get_commands_sent, (), (const, override));
};

} // namespace hub
