// host_test/test_message_dispatcher/main/test_hub_nvs.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <type_traits>

#include "esp_rom_crc.h"
#include "hub_nvs.hpp"
#include "hub_stats.hpp"
#include "mock_persistence_backend.hpp"

using ::testing::_;
using ::testing::NiceMock;

template <typename T> inline uint32_t calculate_test_crc(const T& data)
{
    static_assert(std::is_standard_layout_v<T>, "T must be standard_layout for safe offset calculation");
    static_assert(offsetof(T, crc) != 0, "T must have a non-first crc field");

    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&data), offsetof(T, crc));
}

class HubNvsTest : public ::testing::Test
{
protected:
    NiceMock<MockPersistenceBackend> rtc_backend_;
    NiceMock<MockPersistenceBackend> nvs_backend_;
    std::unique_ptr<HubNvs> sut_;

    void SetUp() override
    {
        rtc_backend_.UseRealStorage();
        nvs_backend_.UseRealStorage();
        sut_ = std::make_unique<HubNvs>(rtc_backend_, nvs_backend_);
    }

    void TearDown() override { sut_.reset(); }

    HubStats create_valid_stats()
    {
        HubStats stats;
        stats.reset();
        stats.magic = HubStats::MAGIC;
        stats.messages_received = 100;
        stats.commands_sent = 50;
        stats.crc = calculate_test_crc(stats);
        return stats;
    }

    void set_rtc_data(const HubStats& stats) { rtc_backend_.save(&stats, sizeof(stats)); }
    void set_nvs_data(const HubStats& stats) { nvs_backend_.save(&stats, sizeof(stats)); }

    HubStats get_stored_rtc_data() const
    {
        HubStats stats;
        memcpy(&stats, rtc_backend_.GetStoredData(), sizeof(stats));
        return stats;
    }

    HubStats get_stored_nvs_data() const
    {
        HubStats stats;
        memcpy(&stats, nvs_backend_.GetStoredData(), sizeof(stats));
        return stats;
    }
};

TEST_F(HubNvsTest, LoadFromRtcWhenValid)
{
    HubStats expected = create_valid_stats();
    set_rtc_data(expected);

    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(0);

    HubStats loaded{};
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(loaded.messages_received, expected.messages_received);
}

TEST_F(HubNvsTest, LoadFromNvsWhenRtcInvalid)
{
    HubStats expected = create_valid_stats();
    set_nvs_data(expected);

    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(1);
    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);

    HubStats loaded{};
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(loaded.messages_received, expected.messages_received);

    HubStats rtc_data = get_stored_rtc_data();
    EXPECT_EQ(rtc_data.messages_received, expected.messages_received);
}

TEST_F(HubNvsTest, LoadFailsWhenBothInvalid)
{
    HubStats loaded{};
    esp_err_t ret = sut_->load_app_data(loaded);
    EXPECT_NE(ret, ESP_OK);
}

TEST_F(HubNvsTest, SaveToRtcOnlyWhenNotForcingNvs)
{
    HubStats to_save = create_valid_stats();
    to_save.messages_received = 999;

    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(0);

    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/false);
    EXPECT_EQ(ret, ESP_OK);

    HubStats rtc_stored = get_stored_rtc_data();
    EXPECT_EQ(rtc_stored.messages_received, 999);
}

TEST_F(HubNvsTest, SaveToBothRtcAndNvsWhenForcing)
{
    HubStats to_save = create_valid_stats();
    to_save.messages_received = 888;

    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(1);

    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/true);
    EXPECT_EQ(ret, ESP_OK);

    HubStats rtc_stored = get_stored_rtc_data();
    HubStats nvs_stored = get_stored_nvs_data();
    EXPECT_EQ(rtc_stored.messages_received, 888);
    EXPECT_EQ(nvs_stored.messages_received, 888);
}

TEST_F(HubNvsTest, RoundTripSaveAndLoad)
{
    HubStats original = create_valid_stats();
    original.messages_received = 777;
    original.commands_sent = 42;

    esp_err_t save_ret = sut_->save_app_data(original, /*force_nvs_commit=*/true);
    EXPECT_EQ(save_ret, ESP_OK);

    HubStats loaded{};
    esp_err_t load_ret = sut_->load_app_data(loaded);

    EXPECT_EQ(load_ret, ESP_OK);
    EXPECT_EQ(loaded.messages_received, 777);
    EXPECT_EQ(loaded.commands_sent, 42);
}

TEST_F(HubNvsTest, LoadFailsWithBadCrc)
{
    HubStats corrupted = create_valid_stats();
    corrupted.crc = 0xDEADBEEF;
    set_nvs_data(corrupted);

    HubStats loaded{};
    esp_err_t ret = sut_->load_app_data(loaded);
    EXPECT_NE(ret, ESP_OK);
}

TEST_F(HubNvsTest, LoadFailsWithWrongMagic)
{
    HubStats wrong_magic = create_valid_stats();
    wrong_magic.magic = 0x12345678;
    wrong_magic.crc = calculate_test_crc(wrong_magic);
    set_nvs_data(wrong_magic);

    HubStats loaded{};
    esp_err_t ret = sut_->load_app_data(loaded);
    EXPECT_NE(ret, ESP_OK);
}
