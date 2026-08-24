// host_test_common/mock_load_domain_controller.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_load_domain_controller.hpp"

class MockLoadDomainController : public ILoadDomainController
{
public:
    MOCK_METHOD(LoadIntent, get_current_intent, (), (const, override));
    MOCK_METHOD(LoadIndex, get_load_index, (), (const, override));
};
