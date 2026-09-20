// SPDX-License-Identifier: MIT
#pragma once

#include "gh/Config.h"
#include "gh/Types.h"

namespace gh {

/// Informacja o czasie sciennym; `valid == false` gdy brak synchronizacji NTP.
struct WallClock {
    uint16_t minuteOfDay = 0;  ///< 0..1439, czas lokalny
    uint32_t dayNumber = 0;    ///< numer doby (do dziennych licznikow)
    bool valid = false;
};

/**
 * @brief Sterowanie fotoperiodem lamp uprawowych (CH4).
 *
 * Ziola liscio we (bazylia, mieta, pietruszka) potrzebuja ok. 14-16 h swiatla.
 * Okno jest definiowane w czasie lokalnym i poprawnie obsluguje przejscie
 * przez polnoc (np. 22:00 -> 06:00).
 *
 * Gdy NTP nie jest dostepny, sterownik przechodzi na cykl licznikowy oparty
 * o czas monotoniczny, aby rosliny nie zostaly bez swiatla przy braku sieci.
 */
class LightController {
 public:
    explicit LightController(const LightConfig& cfg) : cfg_(cfg) {}

    void setConfig(const LightConfig& cfg) { cfg_ = cfg; }
    const LightConfig& config() const { return cfg_; }

    bool update(const WallClock& clock, Millis now) {
        if (!cfg_.enabled) {
            state_ = false;
            return state_;
        }

        if (clock.valid) {
            usingFallback_ = false;
            state_ = inWindow(clock.minuteOfDay, cfg_.onMinuteOfDay, cfg_.offMinuteOfDay);
            return state_;
        }

        // --- Tryb awaryjny bez NTP ------------------------------------
        if (!usingFallback_) {
            usingFallback_ = true;
            fallbackAnchor_ = now;
            state_ = true;  // zaczynamy od fazy swiatla
        }
        const Millis period = cfg_.fallbackOnMs + cfg_.fallbackOffMs;
        if (period == 0) {
            state_ = false;
            return state_;
        }
        const Millis phase = (now - fallbackAnchor_) % period;
        state_ = phase < cfg_.fallbackOnMs;
        return state_;
    }

    /// Czy okno swiatla obejmuje podana minute doby (z obsluga przejscia przez polnoc).
    static bool inWindow(uint16_t minute, uint16_t onMinute, uint16_t offMinute) {
        if (onMinute == offMinute) {
            return false;  // okno zerowe
        }
        if (onMinute < offMinute) {
            return minute >= onMinute && minute < offMinute;
        }
        return minute >= onMinute || minute < offMinute;
    }

    /// Dlugosc fotoperiodu w minutach (do telemetrii i walidacji konfiguracji).
    static uint16_t photoperiodMinutes(uint16_t onMinute, uint16_t offMinute) {
        if (onMinute == offMinute) {
            return 0;
        }
        return onMinute < offMinute ? static_cast<uint16_t>(offMinute - onMinute)
                                    : static_cast<uint16_t>(1440U - onMinute + offMinute);
    }

    bool state() const { return state_; }
    bool usingFallback() const { return usingFallback_; }

 private:
    LightConfig cfg_;
    Millis fallbackAnchor_ = 0;
    bool state_ = false;
    bool usingFallback_ = false;
};

}  // namespace gh
