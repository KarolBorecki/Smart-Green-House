// SPDX-License-Identifier: MIT
#include <unity.h>

#include "gh/ClimateController.h"
#include "gh/Hysteresis.h"

using namespace gh;

static ClimateConfig fastConfig() {
    ClimateConfig c;
    c.heaterMinOnMs = 0;
    c.heaterMinOffMs = 0;
    c.fanCyclePeriodMs = 0;  // wylaczona wymiana cykliczna - testujemy reakcje na warunki
    c.fanPostHeatMs = 0;
    return c;
}

void test_heater_turns_on_below_setpoint(void) {
    ClimateController cc(fastConfig());
    const auto d = cc.update(Reading::of(20.0F), Reading::of(50.0F), true, 1000);
    TEST_ASSERT_TRUE(d.heater);
    TEST_ASSERT_EQUAL_FLOAT(22.0F, d.targetTempC);
}

void test_heater_off_above_setpoint(void) {
    ClimateController cc(fastConfig());
    cc.update(Reading::of(20.0F), Reading::of(50.0F), true, 1000);
    const auto d = cc.update(Reading::of(23.0F), Reading::of(50.0F), true, 2000);
    TEST_ASSERT_FALSE(d.heater);
}

void test_hysteresis_band_keeps_state(void) {
    ClimateController cc(fastConfig());
    cc.update(Reading::of(20.0F), Reading::of(50.0F), true, 1000);  // grzanie ON
    // 22.0 lezy w pasmie histerezy (21.3 - 22.7) - stan ma zostac utrzymany
    const auto d = cc.update(Reading::of(22.0F), Reading::of(50.0F), true, 2000);
    TEST_ASSERT_TRUE(d.heater);
}

void test_night_setpoint_is_lower(void) {
    ClimateController cc(fastConfig());
    const auto d = cc.update(Reading::of(19.0F), Reading::of(50.0F), false, 1000);
    TEST_ASSERT_EQUAL_FLOAT(18.0F, d.targetTempC);
    TEST_ASSERT_FALSE(d.heater);  // 19 C > 18 C nocna nastawa
}

void test_invalid_temperature_disables_heating(void) {
    ClimateController cc(fastConfig());
    cc.update(Reading::of(15.0F), Reading::of(50.0F), true, 1000);
    TEST_ASSERT_TRUE(cc.heaterState());
    const auto d = cc.update(Reading::invalid(), Reading::invalid(), true, 2000);
    TEST_ASSERT_FALSE(d.heater);
}

void test_critical_temperature_blocks_heater(void) {
    ClimateController cc(fastConfig());
    const auto d = cc.update(Reading::of(40.0F), Reading::of(50.0F), true, 1000);
    TEST_ASSERT_FALSE(d.heater);
    TEST_ASSERT_TRUE(d.fan);
}

void test_fan_runs_on_high_humidity(void) {
    ClimateController cc(fastConfig());
    const auto d = cc.update(Reading::of(21.0F), Reading::of(85.0F), true, 1000);
    TEST_ASSERT_TRUE(d.fan);
    TEST_ASSERT_EQUAL_STRING("humidity", d.fanReason);
}

void test_fan_keeps_running_inside_humidity_hysteresis(void) {
    ClimateController cc(fastConfig());
    cc.update(Reading::of(21.0F), Reading::of(85.0F), true, 1000);
    // 68 % < prog 70 %, ale wciaz powyzej 70-5 = 65 % -> wentylator pracuje dalej
    const auto d = cc.update(Reading::of(21.0F), Reading::of(68.0F), true, 2000);
    TEST_ASSERT_TRUE(d.fan);
}

void test_fan_stops_below_hysteresis(void) {
    ClimateController cc(fastConfig());
    cc.update(Reading::of(21.0F), Reading::of(85.0F), true, 1000);
    const auto d = cc.update(Reading::of(21.0F), Reading::of(60.0F), true, 2000);
    TEST_ASSERT_FALSE(d.fan);
}

void test_fan_periodic_circulation(void) {
    ClimateConfig c = fastConfig();
    c.fanCyclePeriodMs = 1000;
    c.fanCycleOnMs = 200;
    ClimateController cc(c);
    TEST_ASSERT_TRUE(cc.update(Reading::of(21.0F), Reading::of(50.0F), true, 100).fan);
    TEST_ASSERT_FALSE(cc.update(Reading::of(21.0F), Reading::of(50.0F), true, 500).fan);
}

void test_minimum_on_time_prevents_relay_chatter(void) {
    ClimateConfig c;
    c.heaterMinOnMs = 60000;
    c.heaterMinOffMs = 60000;
    c.fanCyclePeriodMs = 0;
    ClimateController cc(c);

    cc.update(Reading::of(15.0F), Reading::of(50.0F), true, 100000);  // ON
    TEST_ASSERT_TRUE(cc.heaterState());
    // Gwaltowny skok temperatury po 1 s - przekaznik ma NIE przelaczyc
    cc.update(Reading::of(30.0F), Reading::of(50.0F), true, 101000);
    TEST_ASSERT_TRUE(cc.heaterState());
    // Po uplywie minimalnego czasu zalaczenia - przelaczenie dozwolone
    cc.update(Reading::of(30.0F), Reading::of(50.0F), true, 161000);
    TEST_ASSERT_FALSE(cc.heaterState());
}

void test_hysteresis_switch_first_call_is_not_blocked(void) {
    HysteresisSwitch sw(60000, 60000);
    TEST_ASSERT_TRUE(sw.update(true, false, 5000));
}
