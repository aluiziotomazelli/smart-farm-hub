// host_test_common/mock_load_control_task.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_load_control_task.hpp"

namespace hub {

class MockLoadControlTask : public ILoadControlTask {
public:
    MOCK_METHOD(esp_err_t, post_solar_update, (const SolarPowerUpdate& update), (override));
    MOCK_METHOD(esp_err_t, post_load_intent, (const LoadIntent& intent), (override));
    MOCK_METHOD(esp_err_t, post_load_status, (const LoadStatusUpdate& status), (override));
    MOCK_METHOD(esp_err_t, notify_energy_availability, (bool solar_available, bool grid_available), (override));
};

} // namespace hub
