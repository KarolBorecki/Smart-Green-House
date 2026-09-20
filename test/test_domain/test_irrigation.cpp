// SPDX-License-Identifier: MIT
#include <unity.h>

#include "gh/IrrigationController.h"

using namespace gh;

namespace {

IrrigationConfig testConfig() {
    IrrigationConfig c;
    c.pulseMs = 1000;
    c.soakMs = 2000;
    c.minIntervalMs = 5000;
    c.maxPulsesPerDay = 3;
    c.dryRunPulsesToFault = 2;
    c.dryTraysToTrigger = 1;
    return c;
}

SensorSnapshot snapshot(WaterLevel a, WaterLevel b, WaterLevel c) {
    SensorSnapshot s;
    s.trayCount = 3;
    s.trays[0].water = a;
    s.trays[1].water = b;
    s.trays[2].water = c;
    return s;
}

WallClock day(uint32_t d) {
    WallClock wc;
    wc.valid = true;
    wc.dayNumber = d;
    wc.minuteOfDay = 600;
    return wc;
}

}  // namespace

void test_pump_starts_when_tray_is_dry(void) {
    IrrigationController ic(testConfig());
    const auto d =
        ic.update(snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet), day(1), 1000);
    TEST_ASSERT_TRUE(d.pump);
    TEST_ASSERT_EQUAL(IrrigationState::Pulsing, d.state);
}

void test_pump_does_not_start_when_all_wet(void) {
    IrrigationController ic(testConfig());
    const auto d =
        ic.update(snapshot(WaterLevel::Wet, WaterLevel::Wet, WaterLevel::Wet), day(1), 1000);
    TEST_ASSERT_FALSE(d.pump);
}

void test_unknown_water_state_does_not_trigger_pump(void) {
    // Czujnik bez komunikacji nie moze uruchomic pompy - inaczej awaria I2C
    // zalewalaby uprawe.
    IrrigationController ic(testConfig());
    const auto d = ic.update(
        snapshot(WaterLevel::Unknown, WaterLevel::Unknown, WaterLevel::Unknown), day(1), 1000);
    TEST_ASSERT_FALSE(d.pump);
}

void test_threshold_requires_enough_dry_trays(void) {
    IrrigationConfig cfg = testConfig();
    cfg.dryTraysToTrigger = 2;
    IrrigationController ic(cfg);
    TEST_ASSERT_FALSE(
        ic.update(snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet), day(1), 1000).pump);
    TEST_ASSERT_TRUE(
        ic.update(snapshot(WaterLevel::Dry, WaterLevel::Dry, WaterLevel::Wet), day(1), 2000).pump);
}

void test_pulse_stops_after_configured_time(void) {
    IrrigationController ic(testConfig());
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);
    ic.update(dry, day(1), 1000);
    TEST_ASSERT_TRUE(ic.update(dry, day(1), 1500).pump);
    const auto d = ic.update(dry, day(1), 2100);  // po 1100 ms > pulseMs
    TEST_ASSERT_FALSE(d.pump);
    TEST_ASSERT_EQUAL(IrrigationState::Soaking, d.state);
    TEST_ASSERT_EQUAL_UINT8(1, d.pulsesToday);
}

void test_soak_period_blocks_second_pulse(void) {
    IrrigationController ic(testConfig());
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);
    ic.update(dry, day(1), 1000);
    ic.update(dry, day(1), 2100);                          // -> Soaking
    TEST_ASSERT_FALSE(ic.update(dry, day(1), 3000).pump);  // wciaz wsiaka
}

void test_min_interval_between_cycles(void) {
    IrrigationController ic(testConfig());
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);
    ic.update(dry, day(1), 1000);
    ic.update(dry, day(1), 2100);  // Soaking
    // Symulujemy skuteczne podlanie, zeby nie zliczyc suchobiegu
    const auto wet = snapshot(WaterLevel::Wet, WaterLevel::Wet, WaterLevel::Wet);
    ic.update(wet, day(1), 4200);  // koniec Soaking -> Idle
    TEST_ASSERT_EQUAL(IrrigationState::Idle, ic.state());
    // Znowu sucho, ale nie minal minIntervalMs (5 s od konca cyklu)
    TEST_ASSERT_FALSE(ic.update(dry, day(1), 6000).pump);
    TEST_ASSERT_TRUE(ic.update(dry, day(1), 9300).pump);
}

void test_daily_limit_locks_out_pump(void) {
    IrrigationConfig cfg = testConfig();
    cfg.maxPulsesPerDay = 1;
    cfg.dryRunPulsesToFault = 10;  // izolujemy limit dzienny od detekcji suchobiegu
    IrrigationController ic(cfg);
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);

    ic.update(dry, day(1), 1000);
    ic.update(dry, day(1), 2100);  // pulse 1 zakonczony
    ic.update(dry, day(1), 4200);  // koniec wsiakania -> Idle
    const auto d = ic.update(dry, day(1), 10000);
    TEST_ASSERT_FALSE(d.pump);
    TEST_ASSERT_EQUAL(FaultCode::IrrigationLockout, d.fault);
}

void test_daily_counter_resets_next_day(void) {
    IrrigationConfig cfg = testConfig();
    cfg.maxPulsesPerDay = 1;
    cfg.dryRunPulsesToFault = 10;
    IrrigationController ic(cfg);
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);

    ic.update(dry, day(1), 1000);
    ic.update(dry, day(1), 2100);
    ic.update(dry, day(1), 4200);
    ic.update(dry, day(1), 10000);  // blokada
    TEST_ASSERT_EQUAL(IrrigationState::Fault, ic.state());

    const auto d = ic.update(dry, day(2), 20000);  // nowa doba
    TEST_ASSERT_EQUAL_UINT8(0, d.pulsesToday);
    TEST_ASSERT_NOT_EQUAL(IrrigationState::Fault, d.state);
}

void test_dry_run_detection_blocks_pump(void) {
    // Zbiornik pusty: kolejne impulsy nie zmieniaja stanu czujnikow.
    IrrigationController ic(testConfig());
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);

    ic.update(dry, day(1), 1000);
    ic.update(dry, day(1), 2100);
    ic.update(dry, day(1), 4200);   // cykl 1 nieskuteczny
    ic.update(dry, day(1), 10000);  // cykl 2 start
    ic.update(dry, day(1), 11100);
    const auto d = ic.update(dry, day(1), 13200);

    TEST_ASSERT_EQUAL(FaultCode::ReservoirEmpty, d.fault);
    TEST_ASSERT_EQUAL(IrrigationState::Fault, d.state);
    TEST_ASSERT_FALSE(d.pump);
}

void test_manual_reset_clears_reservoir_fault(void) {
    IrrigationController ic(testConfig());
    const auto dry = snapshot(WaterLevel::Dry, WaterLevel::Wet, WaterLevel::Wet);
    ic.update(dry, day(1), 1000);
    ic.update(dry, day(1), 2100);
    ic.update(dry, day(1), 4200);
    ic.update(dry, day(1), 10000);
    ic.update(dry, day(1), 11100);
    ic.update(dry, day(1), 13200);
    TEST_ASSERT_EQUAL(IrrigationState::Fault, ic.state());

    ic.resetFault();
    TEST_ASSERT_EQUAL(IrrigationState::Idle, ic.state());
    TEST_ASSERT_EQUAL(FaultCode::None, ic.fault());
}

void test_disabled_irrigation_never_runs(void) {
    IrrigationConfig cfg = testConfig();
    cfg.enabled = false;
    IrrigationController ic(cfg);
    TEST_ASSERT_FALSE(
        ic.update(snapshot(WaterLevel::Dry, WaterLevel::Dry, WaterLevel::Dry), day(1), 1000).pump);
}
