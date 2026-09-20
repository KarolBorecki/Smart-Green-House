// SPDX-License-Identifier: MIT
#pragma once

#include "gh/ClimateController.h"
#include "gh/Config.h"
#include "gh/IrrigationController.h"
#include "gh/LightController.h"
#include "gh/Types.h"

namespace gh {

/// Reczne nadpisanie stanu urzadzenia z czasem wygasniecia.
struct ManualOverride {
    bool active = false;
    bool value = false;
    Millis expiresAt = 0;  ///< 0 = bezterminowo
};

/// Usredniony obraz warunkow w komorze.
struct AggregatedReadings {
    Reading temperatureC;
    Reading humidityPct;
    float minTempC = 0.0F;
    float maxTempC = 0.0F;
    uint8_t validTempSensors = 0;
    uint8_t validHumSensors = 0;
    uint8_t dryTrays = 0;
};

/// Pelny stan sterownika - zrodlo dla telemetrii InfluxDB i REST API.
struct GreenhouseStatus {
    ActuatorCommand command;
    AggregatedReadings readings;
    ClimateDecision climate;
    IrrigationDecision irrigation;
    bool lightsOn = false;
    bool lightFallback = false;
    FaultCode fault = FaultCode::None;
    bool safeMode = false;
    Millis uptimeMs = 0;
};

/**
 * @brief Nadrzedny sterownik szklarni - laczy regulatory i nadzor bezpieczenstwa.
 *
 * Kolejnosc decyzji ma znaczenie:
 *  1. agregacja odczytow z tac (odporna na pojedynczy uszkodzony czujnik),
 *  2. fotoperiod (wyznacza nastawe dzien/noc dla klimatu),
 *  3. klimat i nawadnianie,
 *  4. nadzor bezpieczenstwa - moze nadpisac wszystko,
 *  5. reczne nadpisania operatora - nadrzedne wobec automatyki, ale NIE wobec
 *     blokad bezpieczenstwa (przegrzanie zawsze wygrywa).
 */
class GreenhouseController {
 public:
    explicit GreenhouseController(const GreenhouseConfig& cfg)
        : cfg_(cfg), climate_(cfg.climate), light_(cfg.light), irrigation_(cfg.irrigation) {}

    void setConfig(const GreenhouseConfig& cfg) {
        cfg_ = cfg;
        climate_.setConfig(cfg.climate);
        light_.setConfig(cfg.light);
        irrigation_.setConfig(cfg.irrigation);
    }

    const GreenhouseConfig& config() const { return cfg_; }

    GreenhouseStatus update(const SensorSnapshot& snap, const WallClock& clock, Millis now) {
        GreenhouseStatus st;
        st.uptimeMs = now;
        st.readings = aggregate(snap);

        if (st.readings.temperatureC.valid) {
            lastValidSensorMs_ = now;
            sensorsSeen_ = true;
        }

        // 1. Swiatlo
        st.lightsOn = light_.update(clock, now);
        st.lightFallback = light_.usingFallback();

        // 2. Klimat
        st.climate =
            climate_.update(st.readings.temperatureC, st.readings.humidityPct, st.lightsOn, now);

        // 3. Nawadnianie
        st.irrigation = irrigation_.update(snap, clock, now);

        st.command[Actuator::Light] = st.lightsOn;
        st.command[Actuator::Heater] = st.climate.heater;
        st.command[Actuator::Fan] = st.climate.fan;
        st.command[Actuator::Pump] = st.irrigation.pump;

        // 4. Nadzor bezpieczenstwa
        applySafety(st, now);

        // 5. Reczne nadpisania
        applyOverrides(st, now);

        lastStatus_ = st;
        return st;
    }

    /// Ustawia reczne nadpisanie. `ttlMs == 0` oznacza brak wygasania.
    void setOverride(Actuator a, bool value, Millis ttlMs, Millis now) {
        auto& ov = overrides_[static_cast<std::size_t>(a)];
        ov.active = true;
        ov.value = value;
        ov.expiresAt = (ttlMs == 0) ? 0 : (now + ttlMs);
    }

    void clearOverride(Actuator a) { overrides_[static_cast<std::size_t>(a)] = ManualOverride{}; }

    void clearAllOverrides() {
        for (auto& ov : overrides_) {
            ov = ManualOverride{};
        }
    }

    const ManualOverride& overrideFor(Actuator a) const {
        return overrides_[static_cast<std::size_t>(a)];
    }

    void resetFaults() {
        irrigation_.resetFault();
        fault_ = FaultCode::None;
    }

    FaultCode fault() const { return fault_; }
    const GreenhouseStatus& lastStatus() const { return lastStatus_; }

    /**
     * @brief Usrednia odczyty z tac, ignorujac czujniki uszkodzone.
     *
     * Pojedynczy zepsuty SHT-31 nie moze zafalszowac sterowania - liczy sie
     * tylko srednia z czujnikow raportujacych poprawne dane.
     */
    static AggregatedReadings aggregate(const SensorSnapshot& snap) {
        AggregatedReadings agg;
        float tSum = 0.0F;
        float hSum = 0.0F;

        for (std::size_t i = 0; i < snap.trayCount && i < kMaxTrays; ++i) {
            const TrayReading& tr = snap.trays[i];
            if (tr.temperatureC.valid) {
                if (agg.validTempSensors == 0) {
                    agg.minTempC = tr.temperatureC.value;
                    agg.maxTempC = tr.temperatureC.value;
                } else {
                    if (tr.temperatureC.value < agg.minTempC) {
                        agg.minTempC = tr.temperatureC.value;
                    }
                    if (tr.temperatureC.value > agg.maxTempC) {
                        agg.maxTempC = tr.temperatureC.value;
                    }
                }
                tSum += tr.temperatureC.value;
                ++agg.validTempSensors;
            }
            if (tr.humidityPct.valid) {
                hSum += tr.humidityPct.value;
                ++agg.validHumSensors;
            }
            if (tr.water == WaterLevel::Dry) {
                ++agg.dryTrays;
            }
        }

        if (agg.validTempSensors > 0) {
            agg.temperatureC = Reading::of(tSum / static_cast<float>(agg.validTempSensors));
        }
        if (agg.validHumSensors > 0) {
            agg.humidityPct = Reading::of(hSum / static_cast<float>(agg.validHumSensors));
        }
        return agg;
    }

 private:
    void applySafety(GreenhouseStatus& st, Millis now) {
        fault_ = st.irrigation.fault;
        bool safe = false;

        // Przegrzanie - najwyzszy priorytet. Lampy to tez zrodlo ciepla.
        if (st.readings.temperatureC.valid && st.readings.maxTempC >= cfg_.climate.criticalTempC) {
            fault_ = FaultCode::OverTemperature;
            safe = true;
            st.command[Actuator::Heater] = false;
            st.command[Actuator::Light] = false;
            st.command[Actuator::Fan] = true;
            climate_.forceSafe(true, now);
        }

        // Brak wiarygodnych odczytow - nie grzejemy "w ciemno".
        const bool timedOut = !sensorsSeen_
                                  ? (now >= cfg_.safety.sensorTimeoutMs)
                                  : ((now - lastValidSensorMs_) >= cfg_.safety.sensorTimeoutMs);
        if (timedOut && !st.readings.temperatureC.valid) {
            if (fault_ == FaultCode::None || fault_ == FaultCode::IrrigationLockout) {
                fault_ = FaultCode::SensorTimeout;
            }
            safe = true;
            st.command[Actuator::Heater] = false;
            st.command[Actuator::Pump] = false;
            st.command[Actuator::Fan] = cfg_.safety.fanOnFault;
            climate_.forceSafe(cfg_.safety.fanOnFault, now);
        }

        st.fault = fault_;
        st.safeMode = safe;
    }

    void applyOverrides(GreenhouseStatus& st, Millis now) {
        for (std::size_t i = 0; i < kActuatorCount; ++i) {
            ManualOverride& ov = overrides_[i];
            if (!ov.active) {
                continue;
            }
            if (ov.expiresAt != 0 && now >= ov.expiresAt) {
                ov = ManualOverride{};
                continue;
            }
            const Actuator a = static_cast<Actuator>(i);
            // Blokady bezpieczenstwa sa nadrzedne wobec operatora.
            if (st.safeMode && ov.value &&
                (a == Actuator::Heater ||
                 (a == Actuator::Light && fault_ == FaultCode::OverTemperature))) {
                continue;
            }
            st.command[a] = ov.value;
        }
    }

    GreenhouseConfig cfg_;
    ClimateController climate_;
    LightController light_;
    IrrigationController irrigation_;
    ManualOverride overrides_[kActuatorCount];
    GreenhouseStatus lastStatus_;
    Millis lastValidSensorMs_ = 0;
    FaultCode fault_ = FaultCode::None;
    bool sensorsSeen_ = false;
};

}  // namespace gh
