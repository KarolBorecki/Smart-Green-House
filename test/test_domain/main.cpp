// SPDX-License-Identifier: MIT
/**
 * @file main.cpp
 * @brief Runner testow jednostkowych logiki domenowej (Unity, srodowisko native).
 *
 * Testy nie wymagaja sprzetu - caly regulator operuje na czasie i odczytach
 * podawanych z zewnatrz, wiec scenariusze trwajace w rzeczywistosci godziny
 * (np. dzienny limit podlewania) wykonuja sie tu w milisekundach.
 */
#include <unity.h>

// --- test_climate.cpp ---
void test_heater_turns_on_below_setpoint(void);
void test_heater_off_above_setpoint(void);
void test_hysteresis_band_keeps_state(void);
void test_night_setpoint_is_lower(void);
void test_invalid_temperature_disables_heating(void);
void test_critical_temperature_blocks_heater(void);
void test_fan_runs_on_high_humidity(void);
void test_fan_keeps_running_inside_humidity_hysteresis(void);
void test_fan_stops_below_hysteresis(void);
void test_fan_periodic_circulation(void);
void test_minimum_on_time_prevents_relay_chatter(void);
void test_hysteresis_switch_first_call_is_not_blocked(void);

// --- test_light.cpp ---
void test_light_on_inside_window(void);
void test_light_off_outside_window(void);
void test_window_boundaries_are_half_open(void);
void test_window_crossing_midnight(void);
void test_photoperiod_length(void);
void test_fallback_cycle_without_ntp(void);
void test_ntp_recovery_returns_to_schedule(void);
void test_disabled_light_stays_off(void);

// --- test_irrigation.cpp ---
void test_pump_starts_when_tray_is_dry(void);
void test_pump_does_not_start_when_all_wet(void);
void test_unknown_water_state_does_not_trigger_pump(void);
void test_threshold_requires_enough_dry_trays(void);
void test_pulse_stops_after_configured_time(void);
void test_soak_period_blocks_second_pulse(void);
void test_min_interval_between_cycles(void);
void test_daily_limit_locks_out_pump(void);
void test_daily_counter_resets_next_day(void);
void test_dry_run_detection_blocks_pump(void);
void test_manual_reset_clears_reservoir_fault(void);
void test_disabled_irrigation_never_runs(void);

// --- test_controller.cpp ---
void test_aggregation_averages_valid_sensors(void);
void test_aggregation_ignores_broken_sensor(void);
void test_normal_operation_heats_and_lights(void);
void test_over_temperature_forces_safe_state(void);
void test_single_hot_tray_triggers_protection(void);
void test_sensor_timeout_enters_safe_mode(void);
void test_sensor_recovery_clears_timeout(void);
void test_manual_override_turns_pump_on(void);
void test_manual_override_expires(void);
void test_safety_beats_manual_override(void);
void test_override_can_force_device_off(void);
void test_clear_override_restores_automation(void);

// --- test_lineprotocol.cpp ---
void test_float_formatting(void);
void test_tag_escaping(void);
void test_climate_line_structure(void);
void test_climate_line_skips_invalid_readings(void);
void test_actuator_line(void);
void test_tray_line_has_tray_tag(void);
void test_system_line(void);
void test_device_id_with_spaces_is_escaped(void);

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    // Klimat
    RUN_TEST(test_heater_turns_on_below_setpoint);
    RUN_TEST(test_heater_off_above_setpoint);
    RUN_TEST(test_hysteresis_band_keeps_state);
    RUN_TEST(test_night_setpoint_is_lower);
    RUN_TEST(test_invalid_temperature_disables_heating);
    RUN_TEST(test_critical_temperature_blocks_heater);
    RUN_TEST(test_fan_runs_on_high_humidity);
    RUN_TEST(test_fan_keeps_running_inside_humidity_hysteresis);
    RUN_TEST(test_fan_stops_below_hysteresis);
    RUN_TEST(test_fan_periodic_circulation);
    RUN_TEST(test_minimum_on_time_prevents_relay_chatter);
    RUN_TEST(test_hysteresis_switch_first_call_is_not_blocked);

    // Swiatlo
    RUN_TEST(test_light_on_inside_window);
    RUN_TEST(test_light_off_outside_window);
    RUN_TEST(test_window_boundaries_are_half_open);
    RUN_TEST(test_window_crossing_midnight);
    RUN_TEST(test_photoperiod_length);
    RUN_TEST(test_fallback_cycle_without_ntp);
    RUN_TEST(test_ntp_recovery_returns_to_schedule);
    RUN_TEST(test_disabled_light_stays_off);

    // Nawadnianie
    RUN_TEST(test_pump_starts_when_tray_is_dry);
    RUN_TEST(test_pump_does_not_start_when_all_wet);
    RUN_TEST(test_unknown_water_state_does_not_trigger_pump);
    RUN_TEST(test_threshold_requires_enough_dry_trays);
    RUN_TEST(test_pulse_stops_after_configured_time);
    RUN_TEST(test_soak_period_blocks_second_pulse);
    RUN_TEST(test_min_interval_between_cycles);
    RUN_TEST(test_daily_limit_locks_out_pump);
    RUN_TEST(test_daily_counter_resets_next_day);
    RUN_TEST(test_dry_run_detection_blocks_pump);
    RUN_TEST(test_manual_reset_clears_reservoir_fault);
    RUN_TEST(test_disabled_irrigation_never_runs);

    // Sterownik nadrzedny
    RUN_TEST(test_aggregation_averages_valid_sensors);
    RUN_TEST(test_aggregation_ignores_broken_sensor);
    RUN_TEST(test_normal_operation_heats_and_lights);
    RUN_TEST(test_over_temperature_forces_safe_state);
    RUN_TEST(test_single_hot_tray_triggers_protection);
    RUN_TEST(test_sensor_timeout_enters_safe_mode);
    RUN_TEST(test_sensor_recovery_clears_timeout);
    RUN_TEST(test_manual_override_turns_pump_on);
    RUN_TEST(test_manual_override_expires);
    RUN_TEST(test_safety_beats_manual_override);
    RUN_TEST(test_override_can_force_device_off);
    RUN_TEST(test_clear_override_restores_automation);

    // Telemetria
    RUN_TEST(test_float_formatting);
    RUN_TEST(test_tag_escaping);
    RUN_TEST(test_climate_line_structure);
    RUN_TEST(test_climate_line_skips_invalid_readings);
    RUN_TEST(test_actuator_line);
    RUN_TEST(test_tray_line_has_tray_tag);
    RUN_TEST(test_system_line);
    RUN_TEST(test_device_id_with_spaces_is_escaped);

    return UNITY_END();
}
