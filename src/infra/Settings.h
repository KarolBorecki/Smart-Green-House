// SPDX-License-Identifier: MIT
#pragma once

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "gh/Config.h"

namespace gh {

/// Ustawienia lacznosci i integracji - oddzielone od parametrow uprawy.
struct NetworkSettings {
    char wifiSsid[33] = "";
    char wifiPassword[65] = "";
    char hostname[32] = "herb-greenhouse";
    char timezone[48] = "CET-1CEST,M3.5.0,M10.5.0/3";
    char ntpServer1[48] = "pl.pool.ntp.org";
    char ntpServer2[48] = "pool.ntp.org";

    char influxUrl[96] = "";
    char influxOrg[48] = "";
    char influxBucket[48] = "greenhouse";
    char influxToken[128] = "";
    char deviceId[32] = "herb-01";

    char otaPassword[33] = "";
};

/**
 * @brief Trwale ustawienia w LittleFS (JSON) z bezpiecznym zapisem.
 *
 * Zapis idzie do pliku tymczasowego i dopiero potem podmienia oryginal -
 * przerwa w zasilaniu w trakcie zapisu nie zostawia uszkodzonej konfiguracji,
 * co przy szklarni pracujacej bez nadzoru oznaczaloby utrate nastaw uprawy.
 */
class Settings {
 public:
    static constexpr const char* kConfigPath = "/config.json";
    static constexpr const char* kTempPath = "/config.tmp";

    bool begin() {
        if (!LittleFS.begin()) {
            mounted_ = LittleFS.format() && LittleFS.begin();
        } else {
            mounted_ = true;
        }
        return mounted_;
    }

    bool load(GreenhouseConfig& cfg, NetworkSettings& net) {
        if (!mounted_ || !LittleFS.exists(kConfigPath)) {
            return false;
        }
        File f = LittleFS.open(kConfigPath, "r");
        if (!f) {
            return false;
        }
        JsonDocument doc;
        const DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) {
            return false;
        }

        JsonObject c = doc["climate"];
        if (!c.isNull()) {
            cfg.climate.targetTempC = c["target"] | cfg.climate.targetTempC;
            cfg.climate.nightTempC = c["night"] | cfg.climate.nightTempC;
            cfg.climate.tempHysteresisC = c["hysteresis"] | cfg.climate.tempHysteresisC;
            cfg.climate.maxTempC = c["max_temp"] | cfg.climate.maxTempC;
            cfg.climate.criticalTempC = c["critical_temp"] | cfg.climate.criticalTempC;
            cfg.climate.maxHumidityPct = c["max_humidity"] | cfg.climate.maxHumidityPct;
        }
        JsonObject l = doc["light"];
        if (!l.isNull()) {
            cfg.light.onMinuteOfDay = l["on_minute"] | cfg.light.onMinuteOfDay;
            cfg.light.offMinuteOfDay = l["off_minute"] | cfg.light.offMinuteOfDay;
            cfg.light.enabled = l["enabled"] | cfg.light.enabled;
        }
        JsonObject i = doc["irrigation"];
        if (!i.isNull()) {
            cfg.irrigation.enabled = i["enabled"] | cfg.irrigation.enabled;
            cfg.irrigation.pulseMs = i["pulse_ms"] | cfg.irrigation.pulseMs;
            cfg.irrigation.soakMs = i["soak_ms"] | cfg.irrigation.soakMs;
            cfg.irrigation.minIntervalMs = i["min_interval_ms"] | cfg.irrigation.minIntervalMs;
            cfg.irrigation.maxPulsesPerDay =
                i["max_pulses_per_day"] | cfg.irrigation.maxPulsesPerDay;
            cfg.irrigation.dryTraysToTrigger =
                i["dry_trays_trigger"] | cfg.irrigation.dryTraysToTrigger;
        }
        cfg.trayCount = doc["tray_count"] | cfg.trayCount;

        JsonObject n = doc["network"];
        if (!n.isNull()) {
            copyStr(net.wifiSsid, sizeof(net.wifiSsid), n["ssid"] | net.wifiSsid);
            copyStr(net.wifiPassword, sizeof(net.wifiPassword), n["password"] | net.wifiPassword);
            copyStr(net.hostname, sizeof(net.hostname), n["hostname"] | net.hostname);
            copyStr(net.timezone, sizeof(net.timezone), n["timezone"] | net.timezone);
        }
        JsonObject inf = doc["influx"];
        if (!inf.isNull()) {
            copyStr(net.influxUrl, sizeof(net.influxUrl), inf["url"] | net.influxUrl);
            copyStr(net.influxOrg, sizeof(net.influxOrg), inf["org"] | net.influxOrg);
            copyStr(net.influxBucket, sizeof(net.influxBucket), inf["bucket"] | net.influxBucket);
            copyStr(net.influxToken, sizeof(net.influxToken), inf["token"] | net.influxToken);
            copyStr(net.deviceId, sizeof(net.deviceId), inf["device_id"] | net.deviceId);
        }
        return true;
    }

    bool save(const GreenhouseConfig& cfg, const NetworkSettings& net) {
        if (!mounted_) {
            return false;
        }
        JsonDocument doc;
        JsonObject c = doc["climate"].to<JsonObject>();
        c["target"] = cfg.climate.targetTempC;
        c["night"] = cfg.climate.nightTempC;
        c["hysteresis"] = cfg.climate.tempHysteresisC;
        c["max_temp"] = cfg.climate.maxTempC;
        c["critical_temp"] = cfg.climate.criticalTempC;
        c["max_humidity"] = cfg.climate.maxHumidityPct;

        JsonObject l = doc["light"].to<JsonObject>();
        l["on_minute"] = cfg.light.onMinuteOfDay;
        l["off_minute"] = cfg.light.offMinuteOfDay;
        l["enabled"] = cfg.light.enabled;

        JsonObject i = doc["irrigation"].to<JsonObject>();
        i["enabled"] = cfg.irrigation.enabled;
        i["pulse_ms"] = cfg.irrigation.pulseMs;
        i["soak_ms"] = cfg.irrigation.soakMs;
        i["min_interval_ms"] = cfg.irrigation.minIntervalMs;
        i["max_pulses_per_day"] = cfg.irrigation.maxPulsesPerDay;
        i["dry_trays_trigger"] = cfg.irrigation.dryTraysToTrigger;

        doc["tray_count"] = cfg.trayCount;

        JsonObject n = doc["network"].to<JsonObject>();
        n["ssid"] = net.wifiSsid;
        n["password"] = net.wifiPassword;
        n["hostname"] = net.hostname;
        n["timezone"] = net.timezone;

        JsonObject inf = doc["influx"].to<JsonObject>();
        inf["url"] = net.influxUrl;
        inf["org"] = net.influxOrg;
        inf["bucket"] = net.influxBucket;
        inf["token"] = net.influxToken;
        inf["device_id"] = net.deviceId;

        File f = LittleFS.open(kTempPath, "w");
        if (!f) {
            return false;
        }
        const bool ok = serializeJson(doc, f) > 0;
        f.close();
        if (!ok) {
            LittleFS.remove(kTempPath);
            return false;
        }
        LittleFS.remove(kConfigPath);
        return LittleFS.rename(kTempPath, kConfigPath);
    }

    bool mounted() const { return mounted_; }

 private:
    static void copyStr(char* dst, size_t size, const char* src) {
        if (src == nullptr) {
            return;
        }
        strncpy(dst, src, size - 1);
        dst[size - 1] = '\0';
    }

    bool mounted_ = false;
};

}  // namespace gh
