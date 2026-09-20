// SPDX-License-Identifier: MIT
#pragma once

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include <functional>

#include "gh/GreenhouseController.h"
#include "gh/Version.h"
#include "infra/Settings.h"

namespace gh {

/**
 * @brief REST API + prosty panel WWW serwowany z LittleFS.
 *
 * Endpointy:
 *   GET  /api/status        - biezacy stan, pomiary, usterki
 *   GET  /api/config        - aktualne nastawy uprawy
 *   POST /api/config        - zmiana nastaw (JSON, zapis do LittleFS)
 *   POST /api/override      - reczne zalaczenie urzadzenia z czasem wygasniecia
 *   POST /api/faults/reset  - kasowanie blokad (np. po uzupelnieniu wody)
 *   GET  /health            - sonda dla monitoringu
 *
 * Nadpisania recznie zawsze maja TTL (domyslnie 15 min). Zapomniana pompa
 * wlaczona "na chwile" z telefonu to najprostszy sposob na zalanie uprawy.
 */
class WebApi {
 public:
    using StatusProvider = std::function<const GreenhouseStatus&()>;
    using ConfigSaver = std::function<bool(const GreenhouseConfig&)>;

    WebApi(GreenhouseController& controller, GreenhouseConfig& cfg, const NetworkSettings& net)
        : server_(80), controller_(controller), cfg_(cfg), net_(net) {}

    void begin(StatusProvider statusProvider, ConfigSaver configSaver) {
        statusProvider_ = std::move(statusProvider);
        configSaver_ = std::move(configSaver);

        server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
        server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
        server_.on("/api/config", HTTP_POST, [this]() { handlePostConfig(); });
        server_.on("/api/override", HTTP_POST, [this]() { handleOverride(); });
        server_.on("/api/faults/reset", HTTP_POST, [this]() { handleResetFaults(); });
        server_.on("/health", HTTP_GET, [this]() { server_.send(200, "text/plain", "ok"); });

        server_.serveStatic("/", LittleFS, "/index.html");
        server_.serveStatic("/static/", LittleFS, "/static/");
        server_.onNotFound(
            [this]() { server_.send(404, "application/json", "{\"error\":\"not_found\"}"); });
        server_.begin();
    }

    void loop() { server_.handleClient(); }

 private:
    static constexpr uint32_t kDefaultOverrideTtlMs = 15UL * 60UL * 1000UL;
    static constexpr uint32_t kMaxOverrideTtlMs = 6UL * 60UL * 60UL * 1000UL;

    void handleStatus() {
        const GreenhouseStatus& st = statusProvider_();
        JsonDocument doc;
        doc["version"] = firmwareVersion();
        doc["device"] = net_.deviceId;
        doc["uptime_s"] = st.uptimeMs / 1000UL;
        doc["fault"] = toString(st.fault);
        doc["safe_mode"] = st.safeMode;

        JsonObject r = doc["readings"].to<JsonObject>();
        if (st.readings.temperatureC.valid) {
            r["temperature"] = st.readings.temperatureC.value;
            r["temperature_min"] = st.readings.minTempC;
            r["temperature_max"] = st.readings.maxTempC;
        }
        if (st.readings.humidityPct.valid) {
            r["humidity"] = st.readings.humidityPct.value;
        }
        r["sensors_ok"] = st.readings.validTempSensors;
        r["dry_trays"] = st.readings.dryTrays;
        r["target_temperature"] = st.climate.targetTempC;

        JsonObject a = doc["actuators"].to<JsonObject>();
        a["pump"] = st.command[Actuator::Pump];
        a["fan"] = st.command[Actuator::Fan];
        a["heater"] = st.command[Actuator::Heater];
        a["light"] = st.command[Actuator::Light];
        a["fan_reason"] = st.climate.fanReason;

        JsonObject irr = doc["irrigation"].to<JsonObject>();
        irr["state"] = toString(st.irrigation.state);
        irr["pulses_today"] = st.irrigation.pulsesToday;
        irr["fault"] = toString(st.irrigation.fault);

        JsonArray ov = doc["overrides"].to<JsonArray>();
        for (size_t i = 0; i < kActuatorCount; ++i) {
            const ManualOverride& o = controller_.overrideFor(static_cast<Actuator>(i));
            if (!o.active) {
                continue;
            }
            JsonObject item = ov.add<JsonObject>();
            item["actuator"] = toString(static_cast<Actuator>(i));
            item["value"] = o.value;
            item["expires_at_ms"] = o.expiresAt;
        }
        send(200, doc);
    }

    void handleGetConfig() {
        JsonDocument doc;
        JsonObject c = doc["climate"].to<JsonObject>();
        c["target"] = cfg_.climate.targetTempC;
        c["night"] = cfg_.climate.nightTempC;
        c["hysteresis"] = cfg_.climate.tempHysteresisC;
        c["max_temp"] = cfg_.climate.maxTempC;
        c["critical_temp"] = cfg_.climate.criticalTempC;
        c["max_humidity"] = cfg_.climate.maxHumidityPct;

        JsonObject l = doc["light"].to<JsonObject>();
        l["on_minute"] = cfg_.light.onMinuteOfDay;
        l["off_minute"] = cfg_.light.offMinuteOfDay;
        l["enabled"] = cfg_.light.enabled;

        JsonObject i = doc["irrigation"].to<JsonObject>();
        i["enabled"] = cfg_.irrigation.enabled;
        i["pulse_ms"] = cfg_.irrigation.pulseMs;
        i["soak_ms"] = cfg_.irrigation.soakMs;
        i["min_interval_ms"] = cfg_.irrigation.minIntervalMs;
        i["max_pulses_per_day"] = cfg_.irrigation.maxPulsesPerDay;
        send(200, doc);
    }

    void handlePostConfig() {
        JsonDocument doc;
        if (deserializeJson(doc, server_.arg("plain"))) {
            return sendError(400, "invalid_json");
        }

        GreenhouseConfig next = cfg_;
        JsonObject c = doc["climate"];
        if (!c.isNull()) {
            next.climate.targetTempC = c["target"] | next.climate.targetTempC;
            next.climate.nightTempC = c["night"] | next.climate.nightTempC;
            next.climate.tempHysteresisC = c["hysteresis"] | next.climate.tempHysteresisC;
            next.climate.maxTempC = c["max_temp"] | next.climate.maxTempC;
            next.climate.criticalTempC = c["critical_temp"] | next.climate.criticalTempC;
            next.climate.maxHumidityPct = c["max_humidity"] | next.climate.maxHumidityPct;
        }
        JsonObject l = doc["light"];
        if (!l.isNull()) {
            next.light.onMinuteOfDay = l["on_minute"] | next.light.onMinuteOfDay;
            next.light.offMinuteOfDay = l["off_minute"] | next.light.offMinuteOfDay;
            next.light.enabled = l["enabled"] | next.light.enabled;
        }
        JsonObject i = doc["irrigation"];
        if (!i.isNull()) {
            next.irrigation.enabled = i["enabled"] | next.irrigation.enabled;
            next.irrigation.pulseMs = i["pulse_ms"] | next.irrigation.pulseMs;
            next.irrigation.soakMs = i["soak_ms"] | next.irrigation.soakMs;
            next.irrigation.minIntervalMs = i["min_interval_ms"] | next.irrigation.minIntervalMs;
            next.irrigation.maxPulsesPerDay =
                i["max_pulses_per_day"] | next.irrigation.maxPulsesPerDay;
        }

        const char* problem = validate(next);
        if (problem != nullptr) {
            return sendError(422, problem);
        }

        cfg_ = next;
        controller_.setConfig(cfg_);
        const bool persisted = configSaver_ ? configSaver_(cfg_) : false;

        JsonDocument res;
        res["ok"] = true;
        res["persisted"] = persisted;
        send(200, res);
    }

    void handleOverride() {
        JsonDocument doc;
        if (deserializeJson(doc, server_.arg("plain"))) {
            return sendError(400, "invalid_json");
        }
        const char* name = doc["actuator"] | "";
        Actuator actuator;
        if (!parseActuator(name, actuator)) {
            return sendError(400, "unknown_actuator");
        }

        if (doc["clear"] | false) {
            controller_.clearOverride(actuator);
            JsonDocument res;
            res["ok"] = true;
            return send(200, res);
        }

        const bool value = doc["value"] | false;
        uint32_t ttl = doc["ttl_ms"] | kDefaultOverrideTtlMs;
        if (ttl == 0 || ttl > kMaxOverrideTtlMs) {
            ttl = kMaxOverrideTtlMs;  // nadpisanie bezterminowe nie jest dozwolone przez API
        }
        controller_.setOverride(actuator, value, ttl, millis());

        JsonDocument res;
        res["ok"] = true;
        res["actuator"] = toString(actuator);
        res["value"] = value;
        res["ttl_ms"] = ttl;
        send(200, res);
    }

    void handleResetFaults() {
        controller_.resetFaults();
        JsonDocument res;
        res["ok"] = true;
        send(200, res);
    }

    static bool parseActuator(const char* name, Actuator& out) {
        for (size_t i = 0; i < kActuatorCount; ++i) {
            const Actuator a = static_cast<Actuator>(i);
            if (strcmp(name, toString(a)) == 0) {
                out = a;
                return true;
            }
        }
        return false;
    }

    /// Walidacja nastaw - chroni uprawe przed konfiguracja, ktora ja zniszczy.
    static const char* validate(const GreenhouseConfig& c) {
        if (c.climate.targetTempC < 5.0F || c.climate.targetTempC > 35.0F) {
            return "target_out_of_range";
        }
        if (c.climate.nightTempC > c.climate.targetTempC) {
            return "night_above_day";
        }
        if (c.climate.maxTempC <= c.climate.targetTempC) {
            return "max_temp_below_target";
        }
        if (c.climate.criticalTempC <= c.climate.maxTempC) {
            return "critical_temp_below_max";
        }
        if (c.climate.tempHysteresisC < 0.1F || c.climate.tempHysteresisC > 5.0F) {
            return "hysteresis_out_of_range";
        }
        if (c.climate.maxHumidityPct < 30.0F || c.climate.maxHumidityPct > 95.0F) {
            return "humidity_out_of_range";
        }
        if (c.light.onMinuteOfDay > 1439 || c.light.offMinuteOfDay > 1439) {
            return "light_minute_out_of_range";
        }
        if (c.irrigation.pulseMs < 1000UL || c.irrigation.pulseMs > 120000UL) {
            return "pulse_out_of_range";
        }
        if (c.irrigation.soakMs < c.irrigation.pulseMs) {
            return "soak_shorter_than_pulse";
        }
        if (c.irrigation.maxPulsesPerDay == 0 || c.irrigation.maxPulsesPerDay > 60) {
            return "max_pulses_out_of_range";
        }
        return nullptr;
    }

    void send(int code, const JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        server_.send(code, "application/json", out);
    }

    void sendError(int code, const char* reason) {
        JsonDocument doc;
        doc["error"] = reason;
        send(code, doc);
    }

    ESP8266WebServer server_;
    GreenhouseController& controller_;
    GreenhouseConfig& cfg_;
    const NetworkSettings& net_;
    StatusProvider statusProvider_;
    ConfigSaver configSaver_;
};

}  // namespace gh
