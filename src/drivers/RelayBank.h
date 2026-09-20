// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>

#include "config/Pins.h"
#include "gh/Ports.h"

namespace gh {

/**
 * @brief Adapter modulu 8 przekaznikow (uzywane kanaly 1-4).
 *
 * Wlasciwosci istotne dla trwalosci instalacji:
 *  - wyjscia ustawiane sa w stan nieaktywny JESZCZE przed `pinMode(OUTPUT)`,
 *    inaczej przy starcie na ulamek sekundy zalaczylyby sie wszystkie
 *    odbiorniki (pompa i grzalki wlacznie),
 *  - zalaczenia sa rozsuwane w czasie (`kStaggerMs`), aby nie sumowac udarow
 *    pradowych z zasilacza 12 V przy jednoczesnym starcie kilku obciazen,
 *  - zliczany jest czas pracy kazdego urzadzenia - przydatny w telemetrii
 *    (np. zuzycie wody = czas pracy pompy x wydajnosc 240 l/h).
 */
class RelayBank : public IActuatorBank {
 public:
    RelayBank() {
        pins_[static_cast<size_t>(Actuator::Pump)] = GH_PIN_RELAY_PUMP;
        pins_[static_cast<size_t>(Actuator::Fan)] = GH_PIN_RELAY_FAN;
        pins_[static_cast<size_t>(Actuator::Heater)] = GH_PIN_RELAY_HEATER;
        pins_[static_cast<size_t>(Actuator::Light)] = GH_PIN_RELAY_LIGHT;
    }

    bool begin() override {
        for (size_t i = 0; i < kActuatorCount; ++i) {
            digitalWrite(pins_[i], inactiveLevel());
            pinMode(pins_[i], OUTPUT);
            digitalWrite(pins_[i], inactiveLevel());
            states_[i] = false;
            runtimeMs_[i] = 0;
            lastChangeMs_[i] = millis();
        }
        return true;
    }

    void apply(const ActuatorCommand& cmd) override {
        const uint32_t now = millis();
        bool switchedOn = false;
        for (size_t i = 0; i < kActuatorCount; ++i) {
            const bool desired = cmd.on[i];
            if (desired == states_[i]) {
                continue;
            }
            if (desired && switchedOn) {
                // Rozsuwamy zalaczenia w czasie (udar pradowy zasilacza).
                delay(kStaggerMs);
            }
            writeChannel(i, desired, now);
            switchedOn = switchedOn || desired;
        }
    }

    void allOff() override {
        const uint32_t now = millis();
        for (size_t i = 0; i < kActuatorCount; ++i) {
            writeChannel(i, false, now);
        }
    }

    bool state(Actuator a) const override { return states_[static_cast<size_t>(a)]; }

    Millis runtimeMs(Actuator a) const override {
        const size_t i = static_cast<size_t>(a);
        Millis total = runtimeMs_[i];
        if (states_[i]) {
            total += millis() - lastChangeMs_[i];
        }
        return total;
    }

    /// Szacowana objetosc przepompowanej wody [litry] przy wydajnosci 240 l/h.
    float pumpedLitres() const {
        return static_cast<float>(runtimeMs(Actuator::Pump)) / 3600000.0F * 240.0F;
    }

 private:
    static constexpr uint8_t kStaggerMs = 120;

    static int inactiveLevel() { return GH_RELAY_ACTIVE_LEVEL == LOW ? HIGH : LOW; }
    static int activeLevel() { return GH_RELAY_ACTIVE_LEVEL; }

    void writeChannel(size_t i, bool on, uint32_t now) {
        if (states_[i] == on) {
            return;
        }
        if (states_[i]) {
            runtimeMs_[i] += now - lastChangeMs_[i];
        }
        digitalWrite(pins_[i], on ? activeLevel() : inactiveLevel());
        states_[i] = on;
        lastChangeMs_[i] = now;
    }

    uint8_t pins_[kActuatorCount] = {0, 0, 0, 0};
    bool states_[kActuatorCount] = {false, false, false, false};
    uint32_t runtimeMs_[kActuatorCount] = {0, 0, 0, 0};
    uint32_t lastChangeMs_[kActuatorCount] = {0, 0, 0, 0};
};

}  // namespace gh
