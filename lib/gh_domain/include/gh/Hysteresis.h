// SPDX-License-Identifier: MIT
#pragma once

#include "gh/Types.h"

namespace gh {

/**
 * @brief Przelacznik dwustanowy z histereza i minimalnym czasem trwania stanu.
 *
 * Rozwiazuje dwa realne problemy tej instalacji:
 *  - drgania na granicy nastawy (histereza),
 *  - "klekotanie" przekaznika przy szybkich zmianach (minimalny czas stanu).
 *
 * Klasa jest bezstanowa wzgledem sprzetu - operuje wylacznie na czasie
 * monotonicznym podanym z zewnatrz, co czyni ja w pelni testowalna.
 */
class HysteresisSwitch {
 public:
    HysteresisSwitch() = default;
    HysteresisSwitch(Millis minOnMs, Millis minOffMs) : minOnMs_(minOnMs), minOffMs_(minOffMs) {}

    void configure(Millis minOnMs, Millis minOffMs) {
        minOnMs_ = minOnMs;
        minOffMs_ = minOffMs;
    }

    /**
     * @brief Aktualizuje stan na podstawie zadan zalaczenia/wylaczenia.
     * @param riseRequest  warunek zalaczenia jest spelniony
     * @param fallRequest  warunek wylaczenia jest spelniony
     * @param now          czas monotoniczny [ms]
     * @return aktualny stan wyjscia
     *
     * Gdy oba warunki sa falszywe, stan jest utrzymywany (strefa histerezy).
     */
    bool update(bool riseRequest, bool fallRequest, Millis now) {
        if (!initialised_) {
            // Pierwsze wywolanie nie moze byc blokowane przez minimalny czas
            // stanu - inaczej po restarcie grzanie ruszyloby dopiero po minucie.
            initialised_ = true;
            const Millis backdate = minOnMs_ > minOffMs_ ? minOnMs_ : minOffMs_;
            lastChange_ = now - backdate;
        }
        const Millis elapsed = now - lastChange_;

        if (state_) {
            if (fallRequest && elapsed >= minOnMs_) {
                setState(false, now);
            }
        } else {
            if (riseRequest && elapsed >= minOffMs_) {
                setState(true, now);
            }
        }
        return state_;
    }

    /// Wymusza stan z pominieciem ograniczen czasowych (tryb awaryjny/manualny).
    void force(bool state, Millis now) { setState(state, now); }

    bool state() const { return state_; }
    Millis lastChange() const { return lastChange_; }
    Millis timeInStateMs(Millis now) const { return now - lastChange_; }

 private:
    void setState(bool s, Millis now) {
        if (s != state_) {
            state_ = s;
            lastChange_ = now;
        }
        initialised_ = true;
    }

    Millis minOnMs_ = 0;
    Millis minOffMs_ = 0;
    Millis lastChange_ = 0;
    bool state_ = false;
    bool initialised_ = false;
};

/**
 * @brief Pomocnik: klasyczna histereza progowa dla wielkosci narastajacej.
 * @return true gdy nalezy zalaczyc (value < low), false gdy wylaczyc (value > high).
 */
inline bool belowLowThreshold(float value, float target, float hysteresis) {
    return value < (target - hysteresis);
}

inline bool aboveHighThreshold(float value, float target, float hysteresis) {
    return value > (target + hysteresis);
}

}  // namespace gh
