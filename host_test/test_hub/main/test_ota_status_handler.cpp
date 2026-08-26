// host_test/test_hub/main/test_ota_status_handler.cpp
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "handlers/ota_status_handler.hpp"
#include "farm_protocol_types.hpp"
#include "espnow_types.hpp"

using ::testing::_;
using ::testing::StrictMock;

class MockVersionCallback
{
public:
    MOCK_METHOD(void, on_version, (uint8_t node_id, uint8_t major, uint8_t minor, uint8_t patch));
};

TEST(OtaStatusHandlerTest, HandlePayload_InvalidPayloadLength_ReturnsErrorInvalidData)
{
    hub::OtaStatusHandler handler(nullptr);

    uint8_t short_payload[2] = {0x01, 0x02};
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::WATER_TANK);
    std::memcpy(msg.payload, short_payload, sizeof(short_payload));
    msg.payload_len = sizeof(short_payload); // < sizeof(farm::OtaStatusReport)

    espnow::AckStatus status = handler.handle_payload(msg);
    EXPECT_EQ(status, espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST(OtaStatusHandlerTest, HandlePayload_ValidReportWithoutCallback_ReturnsOk)
{
    hub::OtaStatusHandler handler(nullptr);

    farm::OtaStatusReport report{};
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.result = farm::OtaExecResult::CONFIRMED_SUCCESS;
    report.error_code = farm::OtaErrorCode::NONE;
    report.fw_major = 1;
    report.fw_minor = 2;
    report.fw_patch = 3;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::WATER_TANK);
    std::memcpy(msg.payload, &report, sizeof(report));
    msg.payload_len = sizeof(report);

    espnow::AckStatus status = handler.handle_payload(msg);
    EXPECT_EQ(status, espnow::AckStatus::OK);
}

TEST(OtaStatusHandlerTest, HandlePayload_ValidReportWithCallback_TriggersCallbackAndReturnsOk)
{
    MockVersionCallback mock_cb;
    hub::OtaStatusHandler handler([&mock_cb](uint8_t node_id, uint8_t major, uint8_t minor, uint8_t patch) {
        mock_cb.on_version(node_id, major, minor, patch);
    });

    farm::OtaStatusReport report{};
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.result = farm::OtaExecResult::CONFIRMED_SUCCESS;
    report.error_code = farm::OtaErrorCode::NONE;
    report.fw_major = 2;
    report.fw_minor = 0;
    report.fw_patch = 1;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    std::memcpy(msg.payload, &report, sizeof(report));
    msg.payload_len = sizeof(report);

    EXPECT_CALL(mock_cb, on_version(static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL), 2, 0, 1)).Times(1);

    espnow::AckStatus status = handler.handle_payload(msg);
    EXPECT_EQ(status, espnow::AckStatus::OK);
}
