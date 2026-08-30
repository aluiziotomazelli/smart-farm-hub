// host_test/test_hub/main/test_tank_controller.cpp
#include <gtest/gtest.h>

#include "mock_time_manager.hpp"
#include "sun_schedule.hpp"
#include "tank_controller.hpp"

using namespace time_manager;
using ::testing::Return;

static constexpr float LAT_SP = -23.0f;
static constexpr float TZ_SP = -3.0f;

// Helper to construct local epoch: year, month, day, hour, min
static time_t make_epoch(int year, int month, int day, int hour, int min, float tz_offset = TZ_SP)
{
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = 0;
    t.tm_isdst = 0;
    time_t utc_epoch = timegm(&t);
    return utc_epoch - static_cast<time_t>(tz_offset * 3600.0f);
}

class TankControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sun_ = SunSchedule(LAT_SP, TZ_SP);
    }

    MockTimeManager time_mgr_;
    SunSchedule sun_;
};

TEST_F(TankControllerTest, InitialStateIsSatisfiedAt1000Permille)
{
    TankController tc(time_mgr_, sun_);
    EXPECT_EQ(tc.get_state(), TankState::IDLE);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.load_index, LoadIndex::PUMP);
    EXPECT_EQ(intent.desired_state, LoadDesiredState::OFF);
    EXPECT_EQ(intent.estimated_on_duration_s, 0);
}

TEST_F(TankControllerTest, DurationCalculationWithoutMargin)
{
    TankController tc(time_mgr_, sun_);

    // Level 760‰ is in OPPORTUNISTIC tier (target is 1000‰)
    // Deficit: 1000 - 760 = 240‰
    // Rate: 4.8 ‰/min = 0.080 ‰/sec
    // Duration: 240 / 0.080 = 3000 seconds
    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(760, /*float_switch_full=*/false, /*backup_mode=*/false, noon);

    EXPECT_EQ(tc.get_current_tier(), TankFillTier::OPPORTUNISTIC);
    EXPECT_EQ(tc.calculate_estimated_duration_s(), 3000);
}

TEST_F(TankControllerTest, CriticalLevelTargetsNormalMinPermille)
{
    TankController tc(time_mgr_, sun_);

    // Level 200‰ is in CRITICAL_RECOVERY tier (target is normal_min_permille = 500‰)
    // Deficit: 500 - 200 = 300‰
    // Rate: 4.8 ‰/min = 0.080 ‰/sec
    // Duration: 300 / 0.080 = 3750 seconds
    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(200, false, false, noon);

    EXPECT_EQ(tc.get_current_tier(), TankFillTier::CRITICAL_RECOVERY);
    EXPECT_EQ(tc.calculate_estimated_duration_s(), 3750);
}

TEST_F(TankControllerTest, FloatSwitchGovernsSatisfactionInBackupMode)
{
    TankController tc(time_mgr_, sun_);

    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    // In backup mode, float switch open (false) -> Requests fill
    tc.on_tank_report(0, /*float_switch_full=*/false, /*backup_mode=*/true, noon);
    EXPECT_EQ(tc.get_state(), TankState::FILL_REQUESTED);
    EXPECT_EQ(tc.get_current_intent().desired_state, LoadDesiredState::ON);

    // Float switch trips (true) -> Becomes IDLE / OFF
    tc.on_tank_report(0, /*float_switch_full=*/true, /*backup_mode=*/true, noon);
    EXPECT_EQ(tc.get_state(), TankState::IDLE);
    EXPECT_EQ(tc.get_current_intent().desired_state, LoadDesiredState::OFF);
}

TEST_F(TankControllerTest, TargetPermilleReachedTransitionsToIdle)
{
    TankController tc(time_mgr_, sun_);

    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(800, false, false, noon);
    EXPECT_EQ(tc.get_state(), TankState::FILL_REQUESTED);

    // Reaches 1000‰
    tc.on_tank_report(1000, false, false, noon);
    EXPECT_EQ(tc.get_state(), TankState::IDLE);
    EXPECT_EQ(tc.get_current_intent().desired_state, LoadDesiredState::OFF);
}

TEST_F(TankControllerTest, DaytimeHighLevelIsOpportunisticSolarOnly)
{
    TankController tc(time_mgr_, sun_);

    // Noon, level 920‰ (>= 900‰ opportunistic threshold)
    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(920, false, false, noon);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.urgency, LoadUrgency::OPPORTUNISTIC);
    EXPECT_EQ(intent.source_preference, SourcePreference::SOLAR_ONLY);
}

TEST_F(TankControllerTest, DaytimeModerateLevelIsOpportunisticSolarPreferred)
{
    TankController tc(time_mgr_, sun_);

    // Noon, level 700‰ (500 <= level < 900)
    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(700, false, false, noon);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.urgency, LoadUrgency::OPPORTUNISTIC);
    EXPECT_EQ(intent.source_preference, SourcePreference::SOLAR_PREFERRED);
}

TEST_F(TankControllerTest, DaytimeLowLevelIsNormalSolarPreferred)
{
    TankController tc(time_mgr_, sun_);

    // Noon, level 400‰ (< 500‰)
    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(400, false, false, noon);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.urgency, LoadUrgency::NORMAL);
    EXPECT_EQ(intent.source_preference, SourcePreference::SOLAR_PREFERRED);
}

TEST_F(TankControllerTest, PreSunsetWindowEscalatesUrgencyToFillToTop)
{
    TankController tc(time_mgr_, sun_);

    // Sunset is ~18:00 on March 21.
    // 16:30 is 1.5h before sunset (within 2.5h pre-sunset window)
    // Level is 750‰ (normally Opportunistic, but should escalate to Normal)
    time_t afternoon = make_epoch(2026, 3, 21, 16, 30);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(afternoon));

    tc.on_tank_report(750, false, false, afternoon);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.urgency, LoadUrgency::OPPORTUNISTIC);
}

TEST_F(TankControllerTest, NighttimeBlocksNonCriticalFill)
{
    TankController tc(time_mgr_, sun_);

    // 22:00 at night, level 600‰ (not critical) -> Should NOT fill
    time_t night = make_epoch(2026, 3, 21, 22, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(night));

    tc.on_tank_report(600, false, false, night);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::OFF);
    EXPECT_EQ(intent.estimated_on_duration_s, 0);
}

TEST_F(TankControllerTest, CriticalLevelTriggersEmergencyFillEvenAtNight)
{
    TankController tc(time_mgr_, sun_);

    // 22:00 at night, level 250‰ (< 300‰ critical threshold) -> Emergency fill on ANY source
    time_t night = make_epoch(2026, 3, 21, 22, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(night));

    tc.on_tank_report(250, false, false, night);

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.urgency, LoadUrgency::CRITICAL);
    EXPECT_EQ(intent.source_preference, SourcePreference::ANY);
    EXPECT_EQ(tc.get_current_tier(), TankFillTier::CRITICAL_RECOVERY);
}

TEST_F(TankControllerTest, ManualFillButtonForcesFill)
{
    TankController tc(time_mgr_, sun_);

    // At night (22:00) with 800‰ level, normal policy would be OFF.
    // Manual button forces fill!
    time_t night = make_epoch(2026, 3, 21, 22, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(night));

    tc.on_tank_report(800, false, false, night);
    EXPECT_EQ(tc.get_current_intent().desired_state, LoadDesiredState::OFF);

    tc.on_manual_fill_request();

    LoadIntent intent = tc.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.urgency, LoadUrgency::NORMAL);
}

TEST_F(TankControllerTest, OperatorManualStopEntersCooldownAndClearsManualRequest)
{
    TankController tc(time_mgr_, sun_);

    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(700, false, false, noon);
    EXPECT_EQ(tc.get_state(), TankState::FILL_REQUESTED);

    // Pump turns ON
    tc.on_pump_status_update(farm::LoadState::RUNNING, farm::PowerSource::SOLAR, 10);
    EXPECT_EQ(tc.get_state(), TankState::FILLING);

    // Operator stops pump locally (clean IDLE transition while level < 1000)
    tc.on_pump_status_update(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN, 0);
    EXPECT_EQ(tc.get_state(), TankState::IDLE);
    EXPECT_TRUE(tc.is_manual_stop_in_cooldown());

    // During cooldown, subsequent non-critical reports remain IDLE
    tc.on_tank_report(700, false, false, noon + 300); // 5 min later
    EXPECT_EQ(tc.get_state(), TankState::IDLE);
    EXPECT_EQ(tc.get_current_intent().desired_state, LoadDesiredState::OFF);

    // Manual button click overrides cooldown immediately
    tc.on_manual_fill_request();
    EXPECT_FALSE(tc.is_manual_stop_in_cooldown());
    EXPECT_EQ(tc.get_state(), TankState::FILL_REQUESTED);
    EXPECT_EQ(tc.get_current_intent().desired_state, LoadDesiredState::ON);
}

TEST_F(TankControllerTest, CriticalLevelOverridesManualStopCooldown)
{
    TankController tc(time_mgr_, sun_);

    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(700, false, false, noon);
    tc.on_pump_status_update(farm::LoadState::RUNNING, farm::PowerSource::SOLAR, 10);
    tc.on_pump_status_update(farm::LoadState::IDLE, farm::PowerSource::UNKNOWN, 0);
    EXPECT_TRUE(tc.is_manual_stop_in_cooldown());

    // Emergency critical level (<300‰) breaks cooldown
    tc.on_tank_report(200, false, false, noon + 300);
    EXPECT_EQ(tc.get_state(), TankState::FILL_REQUESTED);
    EXPECT_EQ(tc.get_current_tier(), TankFillTier::CRITICAL_RECOVERY);
    EXPECT_EQ(tc.get_current_intent().urgency, LoadUrgency::CRITICAL);
}

TEST_F(TankControllerTest, ErrorTimeoutDoesNotEnterManualStopCooldown)
{
    TankController tc(time_mgr_, sun_);

    time_t noon = make_epoch(2026, 3, 21, 12, 0);
    EXPECT_CALL(time_mgr_, get_timestamp_sec()).WillRepeatedly(Return(noon));

    tc.on_tank_report(700, false, false, noon);
    tc.on_pump_status_update(farm::LoadState::RUNNING, farm::PowerSource::SOLAR, 10);

    // Pump times out (ERROR_TIMEOUT instead of IDLE)
    tc.on_pump_status_update(farm::LoadState::ERROR_TIMEOUT, farm::PowerSource::UNKNOWN, 0);
    EXPECT_FALSE(tc.is_manual_stop_in_cooldown());
    EXPECT_EQ(tc.get_state(), TankState::FILL_REQUESTED);
}
