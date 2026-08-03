// host_test/mocks/mock_i_rotary_encoder.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_rotary_encoder.hpp"

namespace ui_inputs {

class MockIRotaryEncoder : public IRotaryEncoder
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(void, update, (), (override));
    MOCK_METHOD(int32_t, get_steps, (), (override));
};

} // namespace ui_inputs
