// SPDX-License-Identifier: MIT
#pragma once

#include "gh/Config.h"
#include "gh/Hysteresis.h"
#include "gh/Types.h"

namespace gh {

/// Wynik pracy regulatora klimatu.
struct ClimateDecision {
    bool heater = false;
    bool fan = false;
    float targetTempC = 0.0F;
    const char* fanReason = "off";
};

/**
 * @brief Regulator temperatury i wilgotnosci dla komory 140 dm3.
 *
 * Grzanie: dwie maty 12 W sterowane wspolnym kanalem (CH3) z histereza
 * i minimalnym czasem zalaczenia - mala komora ma duza bezwladnosc cieplna
 * w stosunku do mocy grzalek, wiec szybkie przelaczanie nic nie daje,
 * a zuzywa przekaznik.
 *
 * Wentylacja (CH2) zalacza sie z czterech niezaleznych powodow:
 *  1. przegrzanie (temperatura > maxTempC),
 *  2. nadmierna wilgotnosc (ryzyko plesni na ziolach),
 *  3. cykliczna wymiana powietrza (CO2 + hartowanie lodyg),
 *  4. wybieg po grzaniu - rozprowadzenie ciepla z mat po komorze.
 */
class ClimateController {
 public:
    explicit ClimateController(const ClimateConfig& cfg) : cfg_(cfg) {
        heater_.configure(cfg.heaterMinOnMs, cfg.heaterMinOffMs);
    }

    void setConfig(const ClimateConfig& cfg) {
        cfg_ = cfg;
        heater_.configure(cfg.heaterMinOnMs, cfg.heaterMinOffMs);
    }

    const ClimateConfig& config() const { return cfg_; }

    /**
     * @param temperature usredniona temperatura z waznych czujnikow
     * @param humidity    usredniona wilgotnosc wzgledna
     * @param lightsOn    czy trwa faza dnia (inna nastawa temperatury)
     * @param now         czas monotoniczny [ms]
     */
    ClimateDecision update(const Reading& temperature, const Reading& humidity, bool lightsOn,
                           Millis now) {
        ClimateDecision out;
        const float target = lightsOn ? cfg_.targetTempC : cfg_.nightTempC;
        out.targetTempC = target;

        // --- Grzanie ---------------------------------------------------
        bool heaterRise = false;
        bool heaterFall = true;  // brak danych => domyslnie wylacz
        if (temperature.valid) {
            heaterRise = belowLowThreshold(temperature.value, target, cfg_.tempHysteresisC);
            heaterFall = aboveHighThreshold(temperature.value, target, cfg_.tempHysteresisC);
            // Twarde odciecie przy przekroczeniu limitu krytycznego.
            if (temperature.value >= cfg_.criticalTempC) {
                heaterRise = false;
                heaterFall = true;
            }
        }
        const bool heaterWasOn = heater_.state();
        out.heater = heater_.update(heaterRise, heaterFall, now);
        if (!temperature.valid && out.heater) {
            heater_.force(false, now);
            out.heater = false;
        }
        if (heaterWasOn && !out.heater) {
            heaterStoppedAt_ = now;
            heaterEverStopped_ = true;
        }

        // --- Wentylacja ------------------------------------------------
        bool fanRise = false;
        bool fanFall = true;
        const char* reason = "off";

        if (temperature.valid && temperature.value > cfg_.maxTempC) {
            fanRise = true;
            reason = "overtemp";
        } else if (humidity.valid && humidity.value > cfg_.maxHumidityPct) {
            fanRise = true;
            reason = "humidity";
        }

        // Utrzymanie pracy w strefie histerezy (zanim spadniemy ponizej progu).
        if (!fanRise && fan_.state()) {
            const bool stillHot =
                temperature.valid && temperature.value > (cfg_.maxTempC - cfg_.tempHysteresisC);
            const bool stillHumid = humidity.valid && humidity.value > (cfg_.maxHumidityPct -
                                                                        cfg_.humidityHysteresisPct);
            if (stillHot || stillHumid) {
                fanRise = true;
                reason = stillHot ? "overtemp" : "humidity";
            }
        }

        // Wybieg po grzaniu.
        if (!fanRise && heaterEverStopped_ && (now - heaterStoppedAt_) < cfg_.fanPostHeatMs) {
            fanRise = true;
            reason = "post_heat";
        }

        // Cykliczna wymiana powietrza.
        if (!fanRise && cfg_.fanCyclePeriodMs > 0) {
            const Millis phase = now % cfg_.fanCyclePeriodMs;
            if (phase < cfg_.fanCycleOnMs) {
                fanRise = true;
                reason = "circulation";
            }
        }

        fanFall = !fanRise;
        out.fan = fan_.update(fanRise, fanFall, now);
        out.fanReason = out.fan ? reason : "off";
        return out;
    }

    /// Wymusza bezpieczny stan: grzanie off, wentylator wg konfiguracji.
    void forceSafe(bool fanOn, Millis now) {
        heater_.force(false, now);
        fan_.force(fanOn, now);
    }

    bool heaterState() const { return heater_.state(); }
    bool fanState() const { return fan_.state(); }

 private:
    ClimateConfig cfg_;
    HysteresisSwitch heater_;
    HysteresisSwitch fan_{0, 0};
    Millis heaterStoppedAt_ = 0;
    bool heaterEverStopped_ = false;
};

}  // namespace gh
