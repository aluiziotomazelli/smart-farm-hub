// main/include/interfaces/i_load_actuator_dispatcher.hpp
#pragma once

#include "farm_protocol_types.hpp"
#include "load_control_types.hpp"

namespace hub {

/**
 * @interface ILoadActuatorDispatcher
 * @brief Dispatches concrete load actuation commands (ON/OFF/SOURCE) to actuator nodes.
 */
class ILoadActuatorDispatcher {
public:
    virtual ~ILoadActuatorDispatcher() = default;

    /**
     * @brief Dispatches an actuation decision to the target node via transport/command layer.
     * @param decision Arbitrated decision from LoadControlEngine.
     * @return true if command was dispatched/enqueued successfully.
     */
    virtual bool dispatch_decision(const LoadControlDecision& decision) = 0;
};

} // namespace hub
