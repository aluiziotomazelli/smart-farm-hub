// host_test_common/mock_energy_monitor.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_energy_monitor.hpp"

class MockEnergyMonitor : public IEnergyMonitor
{
public:
    MOCK_METHOD(bool, is_solar_available, (), (const, override));
    MOCK_METHOD(bool, is_grid_available, (), (const, override));
};
