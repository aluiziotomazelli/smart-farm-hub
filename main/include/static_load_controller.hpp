// main/include/static_load_controller.hpp
#pragma once

#include "interfaces/i_load_domain_controller.hpp"

/**
 * @class StaticLoadController
 * @brief Generic domain controller for static loads (Fridge, Freezer, Router, Lighting) before sensors are added.
 *
 * Implements ILoadDomainController by wrapping a LoadProfile configuration and translating it into a LoadIntent.
 */
class StaticLoadController : public ILoadDomainController
{
public:
    /**
     * @brief Constructs a static load domain controller.
     * @param load_index The load index represented by this controller.
     * @param profile The static operational and thermal profile.
     * @param default_urgency Default urgency when load is requested ON.
     * @param default_source Default power source preference.
     */
    StaticLoadController(
        LoadIndex load_index,
        const LoadProfile& profile,
        LoadUrgency default_urgency = LoadUrgency::NORMAL,
        SourcePreference default_source = SourcePreference::SOLAR_PREFERRED)
        : load_index_(load_index)
        , profile_(profile)
        , default_urgency_(default_urgency)
        , default_source_(default_source)
    {
    }

    ~StaticLoadController() override = default;

    /**
     * @brief Sets whether this load domain wants to be ON or OFF.
     */
    void set_enabled(bool enabled)
    {
        enabled_ = enabled;
    }

    /**
     * @brief Checks if this load domain is currently enabled.
     */
    bool is_enabled() const
    {
        return enabled_;
    }

    /**
     * @brief Updates the static profile.
     */
    void set_profile(const LoadProfile& profile)
    {
        profile_ = profile;
    }

    /**
     * @brief Gets the current profile.
     */
    const LoadProfile& get_profile() const
    {
        return profile_;
    }

    /** @copydoc ILoadDomainController::get_current_intent */
    LoadIntent get_current_intent() const override
    {
        LoadIntent intent;
        intent.load_index = load_index_;
        intent.desired_state = enabled_ ? LoadDesiredState::ON : LoadDesiredState::OFF;
        intent.source_preference = default_source_;
        intent.urgency = enabled_ ? default_urgency_ : LoadUrgency::SHEDDABLE;
        intent.max_hold_duration_s = profile_.max_shed_duration_s;
        intent.estimated_on_duration_s = profile_.is_continuous ? 0 : 3600; // Continuous loads have 0
        return intent;
    }

    /** @copydoc ILoadDomainController::get_load_index */
    LoadIndex get_load_index() const override
    {
        return load_index_;
    }

private:
    LoadIndex load_index_;
    LoadProfile profile_;
    LoadUrgency default_urgency_;
    SourcePreference default_source_;
    bool enabled_ = true;
};
