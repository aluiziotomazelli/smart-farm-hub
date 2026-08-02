// main/include/app_commands.hpp
#pragma once

#include <cstdint>

#include "protocol_types.hpp"
#include "farm_protocol_types.hpp"

struct AppCommand {
    espnow::CommandType espnow_cmd{espnow::CommandType::REBOOT};
    farm::NodeId target_node{farm::NodeId::HUB};
    uint32_t param{0};
};
