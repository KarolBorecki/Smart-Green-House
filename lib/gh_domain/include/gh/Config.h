// SPDX-License-Identifier: MIT
#pragma once

#include "gh/Types.h"

namespace gh {

/**
 * @brief Parametry klimatu (grzanie matami + wentylacja).
 *
 * Histereza jest podawana jako polowa szerokosci okna wokol wartosci zadanej:
 * grzanie zalacza sie przy `target - hysteresis`, a wylacza przy
 * `target + hysteresis`.
 */
struct ClimateConfig {
    float targetTempC = 22.0F;     ///< temperatura docelowa w dzien
    float nightTempC = 18.0F;      ///< temperatura docelowa noca (lampy off)
    float tempHysteresisC = 0.7F;  ///< histereza grzania
    float maxTempC = 28.0F;        ///< powyzej tej wartosci wymuszamy chlodzenie
    float criticalTempC = 38.0F;   ///< twardy limit - blokada grzania i swiatla
    float maxHumidityPct = 70.0F;  ///< powyzej - wentylacja (ochrona przed plesnia)
    float humidityHysteresisPct = 5.0F;

    /// Minimalny czas zalaczenia/wylaczenia maty (ochrona przekaznika).
    Millis heaterMinOnMs = 60UL * 1000UL;
    Millis heaterMinOffMs = 60UL * 1000UL;

    /// Cykliczna wymiana powietrza niezaleznie od temperatury.
    Millis fanCyclePeriodMs = 30UL * 60UL * 1000UL;  ///< co 30 min
    Millis fanCycleOnMs = 5UL * 60UL * 1000UL;       ///< przez 5 min

    /// Wybieg wentylatora po wylaczeniu grzania (rozprowadzenie ciepla).
    Millis fanPostHeatMs = 30UL * 1000UL;
};

/// Parametry fotoperiodu.
struct LightConfig {
    uint16_t onMinuteOfDay = 6 * 60;    ///< 06:00 czasu lokalnego
    uint16_t offMinuteOfDay = 22 * 60;  ///< 22:00 czasu lokalnego -> 16 h swiatla
    bool enabled = true;
    /// Gdy brak synchronizacji NTP - fallback na cykl licznikowy.
    Millis fallbackOnMs = 16UL * 60UL * 60UL * 1000UL;
    Millis fallbackOffMs = 8UL * 60UL * 60UL * 1000UL;
};

/// Parametry nawadniania (pompa 240 l/h + czujniki LM-393).
struct IrrigationConfig {
    bool enabled = true;
    /// Ile tac musi byc suchych, aby uruchomic pompe (pompa jest wspolna).
    uint8_t dryTraysToTrigger = 1;
    Millis pulseMs = 8UL * 1000UL;                ///< dlugosc jednego podlania
    Millis soakMs = 5UL * 60UL * 1000UL;          ///< przerwa na wsiakniecie przed ponowna ocena
    Millis minIntervalMs = 30UL * 60UL * 1000UL;  ///< min. odstep miedzy cyklami
    uint8_t maxPulsesPerDay = 12;                 ///< dzienny limit bezpieczenstwa
    /// Liczba kolejnych podlan bez zmiany stanu czujnika -> podejrzenie pustego zbiornika.
    uint8_t dryRunPulsesToFault = 3;
};

/// Parametry nadzoru bezpieczenstwa.
struct SafetyConfig {
    /// Po tym czasie bez poprawnego odczytu przechodzimy w tryb bezpieczny.
    Millis sensorTimeoutMs = 3UL * 60UL * 1000UL;
    /// Czy przy usterce czujnikow wentylator ma pracowac (chlodzenie awaryjne).
    bool fanOnFault = true;
};

/// Kompletna konfiguracja sterownika.
struct GreenhouseConfig {
    ClimateConfig climate;
    LightConfig light;
    IrrigationConfig irrigation;
    SafetyConfig safety;

    /// Liczba faktycznie zamontowanych tac (schemat: 3).
    uint8_t trayCount = 3;

    /// Okres petli sterowania.
    Millis controlIntervalMs = 5UL * 1000UL;
    /// Okres wysylki telemetrii do InfluxDB.
    Millis telemetryIntervalMs = 30UL * 1000UL;
};

}  // namespace gh
