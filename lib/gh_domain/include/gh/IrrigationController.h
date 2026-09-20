// SPDX-License-Identifier: MIT
#pragma once

#include "gh/Config.h"
#include "gh/LightController.h"
#include "gh/Types.h"

namespace gh {

/// Faza pracy nawadniania.
enum class IrrigationState : uint8_t {
    Idle = 0,     ///< oczekiwanie na warunek suchosci
    Pulsing = 1,  ///< pompa pracuje
    Soaking = 2,  ///< przerwa na wsiakniecie wody
    Fault = 3     ///< blokada (pusty zbiornik / limit dzienny)
};

inline const char* toString(IrrigationState s) {
    switch (s) {
        case IrrigationState::Idle:
            return "idle";
        case IrrigationState::Pulsing:
            return "pulsing";
        case IrrigationState::Soaking:
            return "soaking";
        case IrrigationState::Fault:
            return "fault";
    }
    return "unknown";
}

struct IrrigationDecision {
    bool pump = false;
    IrrigationState state = IrrigationState::Idle;
    FaultCode fault = FaultCode::None;
    uint8_t dryTrays = 0;
    uint8_t pulsesToday = 0;
};

/**
 * @brief Nawadnianie impulsowe pompa 240 l/h wspolna dla wszystkich tac.
 *
 * Pompa jest jedna, a tace trzy - dlatego decyzja jest zbiorcza: cykl startuje,
 * gdy liczba suchych tac osiagnie prog `dryTraysToTrigger`.
 *
 * Kluczowe zabezpieczenia (pompa zalana woda to najczestsza awaria takich
 * instalacji):
 *  - podlewanie krotkim impulsem + obowiazkowa przerwa na wsiakniecie,
 *    zamiast trzymania pompy do momentu zadzialania czujnika (bezwladnosc
 *    przesiakania podlozem wynosi minuty, nie sekundy),
 *  - minimalny odstep miedzy cyklami,
 *  - dzienny limit impulsow,
 *  - detekcja suchobiegu: jesli kolejne impulsy nie zmieniaja stanu czujnikow,
 *    zbiornik jest najprawdopodobniej pusty i pompa zostaje zablokowana.
 */
class IrrigationController {
 public:
    explicit IrrigationController(const IrrigationConfig& cfg) : cfg_(cfg) {}

    void setConfig(const IrrigationConfig& cfg) { cfg_ = cfg; }
    const IrrigationConfig& config() const { return cfg_; }

    IrrigationDecision update(const SensorSnapshot& snap, const WallClock& clock, Millis now) {
        rolloverDailyCounter(clock, now);

        IrrigationDecision out;
        out.dryTrays = countDry(snap);
        out.pulsesToday = pulsesToday_;

        if (!cfg_.enabled) {
            state_ = IrrigationState::Idle;
            out.state = state_;
            return out;
        }

        switch (state_) {
            case IrrigationState::Idle:
                handleIdle(out, now);
                break;
            case IrrigationState::Pulsing:
                handlePulsing(out, now);
                break;
            case IrrigationState::Soaking:
                handleSoaking(out, now);
                break;
            case IrrigationState::Fault:
                // Wyjscie z blokady: reczny reset albo nowy dzien (limit dzienny).
                if (fault_ == FaultCode::IrrigationLockout && pulsesToday_ == 0) {
                    state_ = IrrigationState::Idle;
                    fault_ = FaultCode::None;
                }
                break;
        }

        out.state = state_;
        out.fault = fault_;
        out.pulsesToday = pulsesToday_;
        out.pump = (state_ == IrrigationState::Pulsing);
        return out;
    }

    /// Reczne skasowanie blokady (np. po uzupelnieniu wody w zbiorniku).
    void resetFault() {
        fault_ = FaultCode::None;
        ineffectivePulses_ = 0;
        if (state_ == IrrigationState::Fault) {
            state_ = IrrigationState::Idle;
        }
    }

    IrrigationState state() const { return state_; }
    FaultCode fault() const { return fault_; }
    uint8_t pulsesToday() const { return pulsesToday_; }

    static uint8_t countDry(const SensorSnapshot& snap) {
        uint8_t dry = 0;
        for (std::size_t i = 0; i < snap.trayCount && i < kMaxTrays; ++i) {
            if (snap.trays[i].water == WaterLevel::Dry) {
                ++dry;
            }
        }
        return dry;
    }

 private:
    void handleIdle(IrrigationDecision& out, Millis now) {
        if (out.dryTrays < cfg_.dryTraysToTrigger) {
            return;
        }
        if (pulsesToday_ >= cfg_.maxPulsesPerDay) {
            state_ = IrrigationState::Fault;
            fault_ = FaultCode::IrrigationLockout;
            return;
        }
        if (hadCycle_ && (now - lastCycleEnd_) < cfg_.minIntervalMs) {
            return;
        }
        dryBeforePulse_ = out.dryTrays;
        pulseStart_ = now;
        state_ = IrrigationState::Pulsing;
    }

    void handlePulsing(IrrigationDecision& out, Millis now) {
        (void)out;
        if ((now - pulseStart_) >= cfg_.pulseMs) {
            soakStart_ = now;
            ++pulsesToday_;
            state_ = IrrigationState::Soaking;
        }
    }

    void handleSoaking(IrrigationDecision& out, Millis now) {
        if ((now - soakStart_) < cfg_.soakMs) {
            return;
        }
        lastCycleEnd_ = now;
        hadCycle_ = true;

        // Skutecznosc impulsu: czy ubylo suchych tac?
        if (out.dryTrays >= dryBeforePulse_) {
            ++ineffectivePulses_;
        } else {
            ineffectivePulses_ = 0;
        }

        if (ineffectivePulses_ >= cfg_.dryRunPulsesToFault) {
            state_ = IrrigationState::Fault;
            fault_ = FaultCode::ReservoirEmpty;
            return;
        }
        state_ = IrrigationState::Idle;
    }

    void rolloverDailyCounter(const WallClock& clock, Millis now) {
        if (clock.valid) {
            if (!dayKnown_ || clock.dayNumber != currentDay_) {
                currentDay_ = clock.dayNumber;
                dayKnown_ = true;
                pulsesToday_ = 0;
            }
            return;
        }
        // Bez NTP: doba liczona od startu urzadzenia.
        static constexpr Millis kDayMs = 24UL * 60UL * 60UL * 1000UL;
        if ((now - fallbackDayStart_) >= kDayMs) {
            fallbackDayStart_ = now;
            pulsesToday_ = 0;
        }
    }

    IrrigationConfig cfg_;
    IrrigationState state_ = IrrigationState::Idle;
    FaultCode fault_ = FaultCode::None;

    Millis pulseStart_ = 0;
    Millis soakStart_ = 0;
    Millis lastCycleEnd_ = 0;
    Millis fallbackDayStart_ = 0;
    bool hadCycle_ = false;

    uint8_t dryBeforePulse_ = 0;
    uint8_t ineffectivePulses_ = 0;
    uint8_t pulsesToday_ = 0;
    uint32_t currentDay_ = 0;
    bool dayKnown_ = false;
};

}  // namespace gh
