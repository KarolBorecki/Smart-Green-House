// SPDX-License-Identifier: MIT
#include <unity.h>

#include <algorithm>
#include <string>

#include "gh/LineProtocol.h"

using namespace gh;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

GreenhouseStatus sampleStatus() {
    GreenhouseStatus st;
    st.readings.temperatureC = Reading::of(21.456F);
    st.readings.humidityPct = Reading::of(58.2F);
    st.readings.minTempC = 20.9F;
    st.readings.maxTempC = 22.1F;
    st.readings.validTempSensors = 3;
    st.readings.dryTrays = 1;
    st.climate.targetTempC = 22.0F;
    st.climate.fanReason = "humidity";
    st.command[Actuator::Fan] = true;
    st.command[Actuator::Light] = true;
    st.irrigation.state = IrrigationState::Soaking;
    st.irrigation.pulsesToday = 2;
    st.uptimeMs = 125000;
    return st;
}

}  // namespace

void test_float_formatting(void) {
    TEST_ASSERT_EQUAL_STRING("21.46", LineProtocol::formatFloat(21.456F).c_str());
    TEST_ASSERT_EQUAL_STRING("-3.50", LineProtocol::formatFloat(-3.5F).c_str());
    TEST_ASSERT_EQUAL_STRING("0.04", LineProtocol::formatFloat(0.04F).c_str());
    TEST_ASSERT_EQUAL_STRING("100.00", LineProtocol::formatFloat(100.0F).c_str());
    TEST_ASSERT_EQUAL_STRING("0.00", LineProtocol::formatFloat(0.0F).c_str());
}

void test_tag_escaping(void) {
    // Przecinki, spacje i znaki rownosci w tagach musza byc poprzedzone
    // ukosnikiem, inaczej InfluxDB odrzuci caly rekord.
    TEST_ASSERT_EQUAL_STRING("herb\\ 01", LineProtocol::escapeTag("herb 01").c_str());
    TEST_ASSERT_EQUAL_STRING("a\\,b\\=c", LineProtocol::escapeTag("a,b=c").c_str());
    TEST_ASSERT_EQUAL_STRING("plain", LineProtocol::escapeTag("plain").c_str());
}

void test_climate_line_structure(void) {
    LineProtocol lp("herb-01");
    const std::string line = lp.climateLine(sampleStatus());
    TEST_ASSERT_TRUE(contains(line, "climate,device=herb-01 "));
    TEST_ASSERT_TRUE(contains(line, "temperature=21.46"));
    TEST_ASSERT_TRUE(contains(line, "humidity=58.20"));
    TEST_ASSERT_TRUE(contains(line, "sensors_ok=3i"));
    TEST_ASSERT_TRUE(contains(line, "dry_trays=1i"));
    // Dokladnie jedna spacja rozdzielajaca tagi od pol
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(std::count(line.begin(), line.end(), ' ')));
}

void test_climate_line_skips_invalid_readings(void) {
    GreenhouseStatus st = sampleStatus();
    st.readings.temperatureC = Reading::invalid();
    st.readings.humidityPct = Reading::invalid();
    LineProtocol lp("herb-01");
    const std::string line = lp.climateLine(st);
    // Uwaga: pole "target_temperature" zawiera podciag "temperature", dlatego
    // sprawdzamy granice pola (spacja lub przecinek przed nazwa).
    TEST_ASSERT_FALSE(contains(line, " temperature="));
    TEST_ASSERT_FALSE(contains(line, ",temperature="));
    TEST_ASSERT_FALSE(contains(line, "humidity="));
    TEST_ASSERT_TRUE(contains(line, "sensors_ok="));
    TEST_ASSERT_TRUE(contains(line, "target_temperature="));
}

void test_actuator_line(void) {
    LineProtocol lp("herb-01");
    const std::string line = lp.actuatorLine(sampleStatus());
    TEST_ASSERT_TRUE(contains(line, "fan=true"));
    TEST_ASSERT_TRUE(contains(line, "heater=false"));
    TEST_ASSERT_TRUE(contains(line, "irrigation_state=\"soaking\""));
    TEST_ASSERT_TRUE(contains(line, "pulses_today=2i"));
}

void test_tray_line_has_tray_tag(void) {
    TrayReading tr;
    tr.temperatureC = Reading::of(21.0F);
    tr.humidityPct = Reading::of(55.0F);
    tr.water = WaterLevel::Dry;
    LineProtocol lp("herb-01");
    const std::string line = lp.trayLine(2, tr);
    TEST_ASSERT_TRUE(contains(line, "tray,device=herb-01,tray=2 "));
    TEST_ASSERT_TRUE(contains(line, "water_dry=true"));
}

void test_system_line(void) {
    LineProtocol lp("herb-01");
    GreenhouseStatus st = sampleStatus();
    st.fault = FaultCode::ReservoirEmpty;
    const std::string line = lp.systemLine(st, 24000, -67);
    TEST_ASSERT_TRUE(contains(line, "uptime_s=125i"));
    TEST_ASSERT_TRUE(contains(line, "free_heap=24000i"));
    TEST_ASSERT_TRUE(contains(line, "rssi=-67i"));
    TEST_ASSERT_TRUE(contains(line, "fault=\"reservoir_empty\""));
}

void test_device_id_with_spaces_is_escaped(void) {
    LineProtocol lp("herb 01");
    const std::string line = lp.climateLine(sampleStatus());
    TEST_ASSERT_TRUE(contains(line, "device=herb\\ 01"));
}
