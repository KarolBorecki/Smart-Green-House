// SPDX-License-Identifier: MIT
#include <unity.h>

#include "gh/LightController.h"

using namespace gh;

static WallClock at(uint16_t minute, uint32_t day = 100) {
    WallClock wc;
    wc.minuteOfDay = minute;
    wc.dayNumber = day;
    wc.valid = true;
    return wc;
}

void test_light_on_inside_window(void) {
    LightController lc{LightConfig{}};
    TEST_ASSERT_TRUE(lc.update(at(12 * 60), 1000));
}

void test_light_off_outside_window(void) {
    LightController lc{LightConfig{}};
    TEST_ASSERT_FALSE(lc.update(at(2 * 60), 1000));
}

void test_window_boundaries_are_half_open(void) {
    LightConfig cfg;
    cfg.onMinuteOfDay = 6 * 60;
    cfg.offMinuteOfDay = 22 * 60;
    LightController lc(cfg);
    TEST_ASSERT_TRUE(lc.update(at(6 * 60), 1000));    // wlacznie z poczatkiem
    TEST_ASSERT_FALSE(lc.update(at(22 * 60), 2000));  // rozlacznie z koncem
    TEST_ASSERT_TRUE(lc.update(at(21 * 60 + 59), 3000));
}

void test_window_crossing_midnight(void) {
    LightConfig cfg;
    cfg.onMinuteOfDay = 22 * 60;  // 22:00
    cfg.offMinuteOfDay = 6 * 60;  // 06:00 nastepnego dnia
    LightController lc(cfg);
    TEST_ASSERT_TRUE(lc.update(at(23 * 60), 1000));
    TEST_ASSERT_TRUE(lc.update(at(3 * 60), 2000));
    TEST_ASSERT_FALSE(lc.update(at(12 * 60), 3000));
}

void test_photoperiod_length(void) {
    TEST_ASSERT_EQUAL_UINT16(960, LightController::photoperiodMinutes(6 * 60, 22 * 60));
    TEST_ASSERT_EQUAL_UINT16(480, LightController::photoperiodMinutes(22 * 60, 6 * 60));
    TEST_ASSERT_EQUAL_UINT16(0, LightController::photoperiodMinutes(300, 300));
}

void test_fallback_cycle_without_ntp(void) {
    LightConfig cfg;
    cfg.fallbackOnMs = 1000;
    cfg.fallbackOffMs = 500;
    LightController lc(cfg);

    WallClock noSync;  // valid == false
    TEST_ASSERT_TRUE(lc.update(noSync, 0));
    TEST_ASSERT_TRUE(lc.usingFallback());
    TEST_ASSERT_TRUE(lc.update(noSync, 900));
    TEST_ASSERT_FALSE(lc.update(noSync, 1200));
    TEST_ASSERT_TRUE(lc.update(noSync, 1600));
}

void test_ntp_recovery_returns_to_schedule(void) {
    LightConfig cfg;
    cfg.fallbackOnMs = 1000;
    cfg.fallbackOffMs = 500;
    LightController lc(cfg);

    WallClock noSync;
    lc.update(noSync, 0);
    TEST_ASSERT_TRUE(lc.usingFallback());

    TEST_ASSERT_FALSE(lc.update(at(3 * 60), 2000));  // 03:00 - poza oknem
    TEST_ASSERT_FALSE(lc.usingFallback());
}

void test_disabled_light_stays_off(void) {
    LightConfig cfg;
    cfg.enabled = false;
    LightController lc(cfg);
    TEST_ASSERT_FALSE(lc.update(at(12 * 60), 1000));
}
