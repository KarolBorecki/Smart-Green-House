// SPDX-License-Identifier: MIT
#pragma once

#include "gh/LightController.h"
#include "gh/Types.h"

namespace gh {

/**
 * @file Ports.h
 * @brief Porty (interfejsy) oddzielajace logike od sprzetu - architektura
 *        heksagonalna. Warstwa `src/drivers` i `src/infra` dostarcza adaptery,
 *        a testy jednostkowe - atrapy.
 */

/// Zrodlo czasu.
class IClock {
 public:
    virtual ~IClock() = default;
    virtual Millis millisNow() const = 0;
    virtual WallClock wallClock() const = 0;
};

/// Magistrala czujnikow (SHT-31 + LM-393 na wszystkich tacach).
class ISensorBus {
 public:
    virtual ~ISensorBus() = default;
    virtual bool begin() = 0;
    virtual SensorSnapshot read() = 0;
};

/// Bank urzadzen wykonawczych (modul 8 przekaznikow).
class IActuatorBank {
 public:
    virtual ~IActuatorBank() = default;
    virtual bool begin() = 0;
    /// Ustawia wszystkie kanaly zgodnie z poleceniem.
    virtual void apply(const ActuatorCommand& cmd) = 0;
    /// Natychmiast wylacza wszystko (watchdog, restart, blad krytyczny).
    virtual void allOff() = 0;
    virtual bool state(Actuator a) const = 0;
    /// Licznik czasu pracy danego urzadzenia [ms] - do konserwacji i telemetrii.
    virtual Millis runtimeMs(Actuator a) const = 0;
};

/// Odbiorca telemetrii (InfluxDB, log szeregowy, bufor offline).
class ITelemetrySink {
 public:
    virtual ~ITelemetrySink() = default;
    virtual bool publish(const struct GreenhouseStatus& status, const SensorSnapshot& snap) = 0;
    virtual bool healthy() const = 0;
};

}  // namespace gh
