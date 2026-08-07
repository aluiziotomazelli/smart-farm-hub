// main/include/handlers/request_time_sync_handler.hpp
#pragma once

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_payload_handler.hpp"

namespace hub {

class RequestTimeSyncHandler : public IPayloadHandler
{
public:
    explicit RequestTimeSyncHandler(CommandManager& command_mgr);

    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;

private:
    CommandManager& command_mgr_;
};

} // namespace hub
