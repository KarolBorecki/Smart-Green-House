// SPDX-License-Identifier: MIT
#include <unity.h>

#include "gh/GreenhouseController.h"

using namespace gh;

namespace {

SensorSnapshot trays(float t0, float t1, float t2, bool valid2 = true) {
    SensorSnapshot s;
    s.trayCount = 3;
    s.trays[0].temperatureC = Reading::of(t0);
    s.trays[0].humidityPct = Reading::of(50.0F);
    s.trays[0].water = WaterLevel::Wet;
    s.trays[1].temperatureC = Reading::of(t1);
    s.trays[1].humidityPct = Reading::of(50.0F);
    s.trays[1].water = WaterLevel::Wet;
    s.trays[2].temperatureC = valid2 ? Reading::of(t2) : Reading::invalid();
    s.trays[2].humidityPct = valid2 ? Reading::of(50.0F) : Reading::invalid();
    s.trays[2].water = WaterLevel::Wet;
    return s;
}

SensorSnapshot allInvalid() {
    SensorSnapshot s;
    s.trayCount = 3;
    for (size_t i = 0; i < 3; ++i) {
        s.trays[i].temperatureC = Reading::invalid();
        s.trays[i].humidityPct = Reading::invalid();
        s.trays[i].water = WaterLevel::Unknown;
    }
    return s;
}

WallClock noon() {
    WallClock wc;
    wc.valid = true;
    wc.minuteOfDay = 12 * 60;
    wc.dayNumber = 42;
    return wc;
}

}  // namespace

void test_aggregation_averages_valid_sensors(void) {
    const auto agg = GreenhouseController::aggregate(trays(20.0F, 22.0F, 24.0F));
    TEST_ASSERT_TRUE(agg.temperatureC.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 22.0F, agg.temperatureC.value);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 20.0F, agg.minTempC);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 24.0F, agg.maxTempC);
    TEST_ASSERT_EQUAL_UINT8(3, agg.validTempSensors);
}

void test_aggregation_ignores_broken_sensor(void) {
    // Uszkodzony czujnik nie moze zanizac sredniej - liczymy tylko sprawne.
    const auto agg = GreenhouseController::aggregate(trays(20.0F, 22.0F, 0.0F, false));
    TEST_ASSERT_EQUAL_UINT8(2, agg.validTempSensors);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 21.0F, agg.temperatureC.value);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 22.0F, agg.maxTempC);
}

void test_normal_operation_heats_and_lights(void) {
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    const auto st = gc.update(trays(18.0F, 18.0F, 18.0F), noon(), 1000);
    TEST_ASSERT_TRUE(st.command[Actuator::Heater]);
    TEST_ASSERT_TRUE(st.command[Actuator::Light]);
    TEST_ASSERT_FALSE(st.command[Actuator::Pump]);
    TEST_ASSERT_EQUAL(FaultCode::None, st.fault);
}

void test_over_temperature_forces_safe_state(void) {
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    const auto st = gc.update(trays(39.0F, 39.0F, 39.0F), noon(), 1000);
    TEST_ASSERT_EQUAL(FaultCode::OverTemperature, st.fault);
    TEST_ASSERT_TRUE(st.safeMode);
    TEST_ASSERT_FALSE(st.command[Actuator::Heater]);
    TEST_ASSERT_FALSE(st.command[Actuator::Light]);  // lampy to tez zrodlo ciepla
    TEST_ASSERT_TRUE(st.command[Actuator::Fan]);
}

void test_single_hot_tray_triggers_protection(void) {
    // Lokalne przegrzanie przy macie grzewczej - srednia jeszcze bezpieczna,
    // ale maksimum przekracza limit krytyczny.
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    const auto st = gc.update(trays(22.0F, 22.0F, 39.0F), noon(), 1000);
    TEST_ASSERT_EQUAL(FaultCode::OverTemperature, st.fault);
    TEST_ASSERT_FALSE(st.command[Actuator::Heater]);
}

void test_sensor_timeout_enters_safe_mode(void) {
    GreenhouseConfig cfg;
    cfg.safety.sensorTimeoutMs = 10000;
    GreenhouseController gc(cfg);

    gc.update(trays(20.0F, 20.0F, 20.0F), noon(), 1000);
    const auto ok = gc.update(allInvalid(), noon(), 5000);
    TEST_ASSERT_EQUAL(FaultCode::None, ok.fault);  // jeszcze w granicach tolerancji

    const auto st = gc.update(allInvalid(), noon(), 20000);
    TEST_ASSERT_EQUAL(FaultCode::SensorTimeout, st.fault);
    TEST_ASSERT_FALSE(st.command[Actuator::Heater]);
    TEST_ASSERT_FALSE(st.command[Actuator::Pump]);
    TEST_ASSERT_TRUE(st.command[Actuator::Fan]);  // chlodzenie awaryjne
}

void test_sensor_recovery_clears_timeout(void) {
    GreenhouseConfig cfg;
    cfg.safety.sensorTimeoutMs = 10000;
    GreenhouseController gc(cfg);
    gc.update(trays(20.0F, 20.0F, 20.0F), noon(), 1000);
    gc.update(allInvalid(), noon(), 20000);
    const auto st = gc.update(trays(20.0F, 20.0F, 20.0F), noon(), 21000);
    TEST_ASSERT_EQUAL(FaultCode::None, st.fault);
    TEST_ASSERT_FALSE(st.safeMode);
}

void test_manual_override_turns_pump_on(void) {
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    gc.setOverride(Actuator::Pump, true, 60000, 1000);
    const auto st = gc.update(trays(22.0F, 22.0F, 22.0F), noon(), 2000);
    TEST_ASSERT_TRUE(st.command[Actuator::Pump]);
}

void test_manual_override_expires(void) {
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    gc.setOverride(Actuator::Pump, true, 5000, 1000);
    TEST_ASSERT_TRUE(gc.update(trays(22.0F, 22.0F, 22.0F), noon(), 3000).command[Actuator::Pump]);
    // Po wygasnieciu TTL pompa wraca pod kontrole automatyki (tace mokre -> off)
    TEST_ASSERT_FALSE(gc.update(trays(22.0F, 22.0F, 22.0F), noon(), 7000).command[Actuator::Pump]);
}

void test_safety_beats_manual_override(void) {
    // Operator wlaczyl grzanie recznie, a komora sie przegrzewa -
    // bezpieczenstwo ma pierwszenstwo.
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    gc.setOverride(Actuator::Heater, true, 600000, 1000);
    const auto st = gc.update(trays(39.0F, 39.0F, 39.0F), noon(), 2000);
    TEST_ASSERT_FALSE(st.command[Actuator::Heater]);
}

void test_override_can_force_device_off(void) {
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    gc.setOverride(Actuator::Light, false, 60000, 1000);
    const auto st = gc.update(trays(22.0F, 22.0F, 22.0F), noon(), 2000);
    TEST_ASSERT_FALSE(st.command[Actuator::Light]);
}

void test_clear_override_restores_automation(void) {
    GreenhouseConfig cfg;
    GreenhouseController gc(cfg);
    gc.setOverride(Actuator::Light, false, 60000, 1000);
    gc.clearOverride(Actuator::Light);
    const auto st = gc.update(trays(22.0F, 22.0F, 22.0F), noon(), 2000);
    TEST_ASSERT_TRUE(st.command[Actuator::Light]);
}
