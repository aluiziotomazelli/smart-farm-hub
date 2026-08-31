// main/include/interfaces/i_load_domain_controller.hpp
#pragma once

#include "load_control_types.hpp"

/**
 * @interface ILoadDomainController
 * @brief Interface for load domain controllers that understand load physical constraints.
 */
class ILoadDomainController
{
public:
    virtual ~ILoadDomainController() = default;

    /**
     * @brief Gets the current operational intent for this load domain.
     * @return LoadIntent describing desired state, urgency, source preference, etc.
     */
    virtual LoadIntent get_current_intent() const = 0;

    /**
     * @brief Gets the logical load index associated with this controller.
     * @return LoadIndex enum value.
     */
    virtual LoadIndex get_load_index() const = 0;
};
