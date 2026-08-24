// host_test_common/mock_hal_energy_monitor.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_hal_energy_monitor.hpp"

namespace idf_hals {

class MockHalEnergyMonitor : public IHalEnergyMonitor
{
public:
    MOCK_METHOD(bool, is_solar_available, (), (const, override));
    MOCK_METHOD(bool, is_grid_available, (), (const, override));
};

} // namespace idf_hals
