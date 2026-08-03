// host_test/mocks/mock_i_button.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_button.hpp"

namespace ui_inputs {

class MockIButton : public IButton
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(void, update, (), (override));
    MOCK_METHOD(ButtonClickType, get_last_click, (), (override));
};

} // namespace ui_inputs
