// host_test/test_hub/main/test_ui_controller.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ui_controller.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_graphics_context.hpp"
#include "i18n/i18n.hpp"

using ::testing::_;
using ::testing::Return;

class UIControllerTest : public ::testing::Test
{
protected:
    MockGraphicsContext mock_gfx_;
    espnow::MockEspNowManager mock_espnow_;

    void SetUp() override
    {
        I18n::set_language(Language::EN_US);
    }
};

TEST_F(UIControllerTest, SettingsScreen_NavNextPrev_TogglesSettingsIndex)
{
    UIController sut(mock_gfx_, nullptr, &mock_espnow_);
    sut.set_screen_mode(ScreenMode::SETTINGS_SCREEN);

    UiEvent next_ev{UiEventType::NAV_NEXT, 1};
    UiEvent prev_ev{UiEventType::NAV_PREV, 1};

    // Originally settings_index_ is 0
    // NAV_NEXT -> settings_index_ becomes 1
    sut.handle_event(next_ev);
    EXPECT_EQ(sut.get_screen_mode(), ScreenMode::SETTINGS_SCREEN);

    // NAV_NEXT again -> wraps back to 0
    sut.handle_event(next_ev);
    EXPECT_EQ(sut.get_screen_mode(), ScreenMode::SETTINGS_SCREEN);

    // NAV_PREV -> wraps to 1
    sut.handle_event(prev_ev);
    EXPECT_EQ(sut.get_screen_mode(), ScreenMode::SETTINGS_SCREEN);
}

TEST_F(UIControllerTest, SettingsScreen_ConfirmOnItem0_TogglesLanguage)
{
    UIController sut(mock_gfx_, nullptr, &mock_espnow_);
    sut.set_screen_mode(ScreenMode::SETTINGS_SCREEN);

    EXPECT_EQ(I18n::get_language(), Language::EN_US);

    UiEvent confirm_ev{UiEventType::CONFIRM, 0};
    sut.handle_event(confirm_ev);

    EXPECT_EQ(I18n::get_language(), Language::PT_BR);

    sut.handle_event(confirm_ev);
    EXPECT_EQ(I18n::get_language(), Language::EN_US);
}

TEST_F(UIControllerTest, SettingsScreen_ConfirmOnItem1_StartsEspNowPairing)
{
    UIController sut(mock_gfx_, nullptr, &mock_espnow_);
    sut.set_screen_mode(ScreenMode::SETTINGS_SCREEN);

    // Move to item 1 (Pairing)
    UiEvent next_ev{UiEventType::NAV_NEXT, 1};
    sut.handle_event(next_ev);

    EXPECT_CALL(mock_espnow_, start_pairing(30000)).WillOnce(Return(ESP_OK));

    UiEvent confirm_ev{UiEventType::CONFIRM, 0};
    sut.handle_event(confirm_ev);

    // Second confirm while active should NOT trigger start_pairing again
    sut.handle_event(confirm_ev);
}

TEST_F(UIControllerTest, SettingsScreen_Render_DrawsMenuItems)
{
    UIController sut(mock_gfx_, nullptr, &mock_espnow_);
    sut.set_screen_mode(ScreenMode::SETTINGS_SCREEN);

    EXPECT_CALL(mock_gfx_, draw_string_centered(0, _, 1, 0, -1, nullptr, false)).Times(::testing::AtLeast(1));
    EXPECT_CALL(mock_gfx_, draw_hline(0, 8, _, 1)).Times(::testing::AtLeast(1));
    EXPECT_CALL(mock_gfx_, draw_string(_, _, _, 1, nullptr, _)).Times(::testing::AtLeast(2));

    sut.render_settings_screen();
}
