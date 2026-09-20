// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#include <cstddef>

/**
 * @file Types.h
 * @brief Podstawowe typy wartosci uzywane w calej domenie sterowania szklarnia.
 *
 * Warstwa `gh` (greenhouse) jest celowo wolna od zaleznosci na Arduino/ESP8266,
 * dzieki czemu kompiluje sie i jest testowana natywnie (g++/clang) w CI.
 */
namespace gh {

/// Maksymalna liczba tac obslugiwanych przez sterownik (wg schematu: 3).
static constexpr std::size_t kMaxTrays = 4;

/// Liczba kanalow modulu przekaznikow.
static constexpr std::size_t kRelayChannels = 8;

/// Czas monotoniczny w milisekundach (jak `millis()`, ale bez Arduino).
using Millis = uint32_t;

/// Czas scienny (UNIX epoch, sekundy UTC).
using EpochSeconds = uint32_t;

/**
 * @brief Odczyt pojedynczej wielkosci fizycznej wraz z informacja o waznosci.
 *
 * Czujnik, ktory nie odpowiada, zwraca `valid == false`. Logika sterowania
 * traktuje taki odczyt jako brak danych i przechodzi w tryb bezpieczny,
 * zamiast dzialac na wartosci domyslnej (np. 0 st. C wlaczyloby grzanie).
 */
struct Reading {
    float value = 0.0F;
    bool valid = false;

    Reading() = default;
    Reading(float v, bool ok) : value(v), valid(ok) {}

    static Reading invalid() { return Reading(0.0F, false); }
    static Reading of(float v) { return Reading(v, true); }
};

/// Stan czujnika poziomu wody LM-393 (komparator progowy).
enum class WaterLevel : uint8_t {
    Unknown = 0,  ///< brak komunikacji z czujnikiem
    Dry = 1,      ///< poziom ponizej progu - podlewanie wskazane
    Wet = 2       ///< poziom powyzej progu
};

/// Pomiar z jednej tacy ziol.
struct TrayReading {
    Reading temperatureC;                    ///< SHT-31
    Reading humidityPct;                     ///< SHT-31
    WaterLevel water = WaterLevel::Unknown;  ///< LM-393
};

/// Komplet pomiarow z calej szklarni w danej chwili.
struct SensorSnapshot {
    TrayReading trays[kMaxTrays];
    std::size_t trayCount = 0;
    Millis timestamp = 0;
};

/// Sterowane urzadzenia (kanaly przekaznika wg schematu).
enum class Actuator : uint8_t {
    Pump = 0,    ///< CH1 - pompa 240 l/h (pump control)
    Fan = 1,     ///< CH2 - wentylator 12V (fan control)
    Heater = 2,  ///< CH3 - maty grzewcze 2x12W (heat control)
    Light = 3    ///< CH4 - lampy uprawowe (light control)
};

static constexpr std::size_t kActuatorCount = 4;

inline const char* toString(Actuator a) {
    switch (a) {
        case Actuator::Pump:
            return "pump";
        case Actuator::Fan:
            return "fan";
        case Actuator::Heater:
            return "heater";
        case Actuator::Light:
            return "light";
    }
    return "unknown";
}

inline const char* toString(WaterLevel w) {
    switch (w) {
        case WaterLevel::Dry:
            return "dry";
        case WaterLevel::Wet:
            return "wet";
        case WaterLevel::Unknown:
            return "unknown";
    }
    return "unknown";
}

/// Zbiorcze polecenie dla wszystkich urzadzen wykonawczych.
struct ActuatorCommand {
    bool on[kActuatorCount] = {false, false, false, false};

    bool& operator[](Actuator a) { return on[static_cast<std::size_t>(a)]; }
    bool operator[](Actuator a) const { return on[static_cast<std::size_t>(a)]; }
};

/// Powod, dla ktorego sterownik znalazl sie w trybie awaryjnym.
enum class FaultCode : uint8_t {
    None = 0,
    SensorTimeout = 1,     ///< brak wiarygodnych odczytow przez zbyt dlugi czas
    OverTemperature = 2,   ///< temperatura powyzej twardego limitu
    ReservoirEmpty = 3,    ///< podlewanie nie podnioslo poziomu - brak wody
    IrrigationLockout = 4  ///< przekroczony dzienny limit podlewania
};

inline const char* toString(FaultCode f) {
    switch (f) {
        case FaultCode::None:
            return "none";
        case FaultCode::SensorTimeout:
            return "sensor_timeout";
        case FaultCode::OverTemperature:
            return "over_temperature";
        case FaultCode::ReservoirEmpty:
            return "reservoir_empty";
        case FaultCode::IrrigationLockout:
            return "irrigation_lockout";
    }
    return "unknown";
}

}  // namespace gh
