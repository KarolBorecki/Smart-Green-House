// SPDX-License-Identifier: MIT
#pragma once

#include <string>

#include "gh/GreenhouseController.h"
#include "gh/Types.h"

namespace gh {

/**
 * @brief Budowanie rekordow InfluxDB Line Protocol.
 *
 * Wydzielone z warstwy sieciowej, aby format danych byl testowany natywnie,
 * bez ESP8266 i bez dzialajacej bazy. Bledny escaping tagow to klasyczne
 * zrodlo "cichych" strat danych w InfluxDB, dlatego ma wlasne testy.
 */
class LineProtocol {
 public:
    explicit LineProtocol(std::string deviceId) : deviceId_(std::move(deviceId)) {}

    /// Escaping wg specyfikacji: przecinek, znak rownosci i spacja w kluczach/tagach.
    static std::string escapeTag(const std::string& in) {
        std::string out;
        out.reserve(in.size() + 4);
        for (char c : in) {
            if (c == ',' || c == '=' || c == ' ') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out;
    }

    static std::string formatFloat(float v, int decimals = 2) {
        // Bez <sstream> - lzejsze dla ESP8266 i deterministyczne.
        bool negative = v < 0.0F;
        if (negative) {
            v = -v;
        }
        long scale = 1;
        for (int i = 0; i < decimals; ++i) {
            scale *= 10;
        }
        const long scaled = static_cast<long>(v * static_cast<float>(scale) + 0.5F);
        const long whole = scaled / scale;
        const long frac = scaled % scale;

        std::string out;
        if (negative && (whole != 0 || frac != 0)) {
            out.push_back('-');
        }
        out += std::to_string(whole);
        if (decimals > 0) {
            out.push_back('.');
            std::string f = std::to_string(frac);
            while (f.size() < static_cast<std::size_t>(decimals)) {
                f.insert(f.begin(), '0');
            }
            out += f;
        }
        return out;
    }

    /// Rekord `climate` - warunki usrednione w komorze.
    std::string climateLine(const GreenhouseStatus& st) const {
        std::string line = "climate,device=" + escapeTag(deviceId_);
        line += " ";
        bool first = true;
        if (st.readings.temperatureC.valid) {
            appendField(line, first, "temperature", formatFloat(st.readings.temperatureC.value));
            appendField(line, first, "temperature_min", formatFloat(st.readings.minTempC));
            appendField(line, first, "temperature_max", formatFloat(st.readings.maxTempC));
        }
        if (st.readings.humidityPct.valid) {
            appendField(line, first, "humidity", formatFloat(st.readings.humidityPct.value));
        }
        appendField(line, first, "target_temperature", formatFloat(st.climate.targetTempC));
        appendField(line, first, "sensors_ok",
                    std::to_string(static_cast<int>(st.readings.validTempSensors)) + "i");
        appendField(line, first, "dry_trays",
                    std::to_string(static_cast<int>(st.readings.dryTrays)) + "i");
        return line;
    }

    /// Rekord `tray` - dane z pojedynczej tacy (tag `tray`).
    std::string trayLine(std::size_t index, const TrayReading& tr) const {
        std::string line = "tray,device=" + escapeTag(deviceId_) + ",tray=" + std::to_string(index);
        line += " ";
        bool first = true;
        if (tr.temperatureC.valid) {
            appendField(line, first, "temperature", formatFloat(tr.temperatureC.value));
        }
        if (tr.humidityPct.valid) {
            appendField(line, first, "humidity", formatFloat(tr.humidityPct.value));
        }
        appendField(line, first, "water_dry", tr.water == WaterLevel::Dry ? "true" : "false");
        appendField(line, first, "sensor_ok", tr.temperatureC.valid ? "true" : "false");
        if (first) {
            return std::string();  // brak pol - nie wysylamy pustego rekordu
        }
        return line;
    }

    /// Rekord `actuators` - stany wyjsc i stan maszyny nawadniania.
    std::string actuatorLine(const GreenhouseStatus& st) const {
        std::string line = "actuators,device=" + escapeTag(deviceId_);
        line += " ";
        bool first = true;
        appendField(line, first, "pump", st.command[Actuator::Pump] ? "true" : "false");
        appendField(line, first, "fan", st.command[Actuator::Fan] ? "true" : "false");
        appendField(line, first, "heater", st.command[Actuator::Heater] ? "true" : "false");
        appendField(line, first, "light", st.command[Actuator::Light] ? "true" : "false");
        appendField(line, first, "irrigation_state", quote(toString(st.irrigation.state)));
        appendField(line, first, "pulses_today",
                    std::to_string(static_cast<int>(st.irrigation.pulsesToday)) + "i");
        appendField(line, first, "fan_reason", quote(st.climate.fanReason));
        return line;
    }

    /// Rekord `system` - zdrowie sterownika.
    std::string systemLine(const GreenhouseStatus& st, uint32_t freeHeapBytes,
                           int32_t wifiRssi) const {
        std::string line = "system,device=" + escapeTag(deviceId_);
        line += " ";
        bool first = true;
        appendField(line, first, "uptime_s", std::to_string(st.uptimeMs / 1000UL) + "i");
        appendField(line, first, "free_heap", std::to_string(freeHeapBytes) + "i");
        appendField(line, first, "rssi", std::to_string(wifiRssi) + "i");
        appendField(line, first, "fault", quote(toString(st.fault)));
        appendField(line, first, "safe_mode", st.safeMode ? "true" : "false");
        appendField(line, first, "ntp_sync", st.lightFallback ? "false" : "true");
        return line;
    }

 private:
    static void appendField(std::string& line, bool& first, const char* key,
                            const std::string& value) {
        if (!first) {
            line.push_back(',');
        }
        first = false;
        line += key;
        line.push_back('=');
        line += value;
    }

    static std::string quote(const char* v) { return std::string("\"") + v + "\""; }

    std::string deviceId_;
};

}  // namespace gh
