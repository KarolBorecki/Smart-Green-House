// SPDX-License-Identifier: MIT
#pragma once

#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <Wire.h>

#include "config/Pins.h"
#include "gh/Ports.h"

namespace gh {

/**
 * @brief Magistrala czujnikow tac: SHT-31 (temperatura/wilgotnosc) + LM-393 (poziom wody).
 *
 * Topologia (patrz komentarz w config/Pins.h):
 *   ESP8266 --I2C--> TCA9548A --> kanal 0..2 --> SHT-31 (adres 0x44 na kazdym kanale)
 *                \-> PCF8574  --> wejscia D0 trzech modulow LM-393
 *
 * Odpornosc na awarie:
 *  - kazdy odczyt jest walidowany (NaN, zakres fizyczny), bledny czujnik
 *    raportuje `valid == false` zamiast wartosci smieciowej,
 *  - po serii bledow czujnik jest reinicjalizowany, a magistrala I2C odblokowana
 *    (klasyczny problem zawieszonego slave trzymajacego SDA w stanie niskim).
 */
class TraySensorBus : public ISensorBus {
 public:
    explicit TraySensorBus(uint8_t trayCount)
        : trayCount_(trayCount > kMaxTrays ? kMaxTrays : trayCount) {}

    bool begin() override {
        Wire.begin(GH_PIN_I2C_SDA, GH_PIN_I2C_SCL);
        Wire.setClock(100000);  // 100 kHz - pewniej na dluzszych przewodach w szklarni
        Wire.setClockStretchLimit(40000);

        bool anyOk = false;
        for (uint8_t i = 0; i < trayCount_; ++i) {
            anyOk |= initSensor(i);
        }
#if !GH_LEVEL_SENSORS_DIRECT
        initExpander();
#else
        initDirectLevelPins();
#endif
        return anyOk;
    }

    SensorSnapshot read() override {
        SensorSnapshot snap;
        snap.trayCount = trayCount_;
        snap.timestamp = millis();

#if !GH_LEVEL_SENSORS_DIRECT
        const uint8_t levelBits = readExpander();
#endif
        for (uint8_t i = 0; i < trayCount_; ++i) {
            TrayReading& tr = snap.trays[i];
            readClimate(i, tr);

#if GH_LEVEL_SENSORS_DIRECT
            tr.water = readLevelDirect(i);
#else
            tr.water = decodeLevelBit(levelBits, i);
#endif
        }
        return snap;
    }

    /// Ustawia liczbe tac przed `begin()` (wartosc z konfiguracji uzytkownika).
    void setTrayCount(uint8_t count) { trayCount_ = count > kMaxTrays ? kMaxTrays : count; }

    uint8_t trayCount() const { return trayCount_; }
    uint16_t errorCount(uint8_t tray) const { return tray < kMaxTrays ? errors_[tray] : 0; }

 private:
    // --- Multiplekser ---------------------------------------------------
    static bool selectChannel(uint8_t channel) {
#if GH_USE_I2C_MUX
        Wire.beginTransmission(GH_I2C_MUX_ADDR);
        Wire.write(static_cast<uint8_t>(1U << channel));
        return Wire.endTransmission() == 0;
#else
        (void)channel;
        return true;
#endif
    }

    static uint8_t sensorAddress(uint8_t tray) {
#if GH_USE_I2C_MUX
        (void)tray;
        return GH_SHT31_ADDR_PRIMARY;  // ten sam adres na kazdym kanale muxa
#else
        return (tray % 2 == 0) ? GH_SHT31_ADDR_PRIMARY : GH_SHT31_ADDR_SECONDARY;
#endif
    }

    bool initSensor(uint8_t tray) {
        if (!selectChannel(tray)) {
            return false;
        }
        const bool ok = sensors_[tray].begin(sensorAddress(tray));
        ready_[tray] = ok;
        return ok;
    }

    void readClimate(uint8_t tray, TrayReading& tr) {
        if (!selectChannel(tray)) {
            registerError(tray, tr);
            return;
        }
        if (!ready_[tray] && !initSensor(tray)) {
            registerError(tray, tr);
            return;
        }

        float t = NAN;
        float h = NAN;
        if (!sensors_[tray].readBoth(&t, &h) || !plausible(t, -40.0F, 90.0F) ||
            !plausible(h, 0.0F, 100.0F)) {
            registerError(tray, tr);
            return;
        }

        errors_[tray] = 0;
        tr.temperatureC = Reading::of(t);
        tr.humidityPct = Reading::of(h);
    }

    void registerError(uint8_t tray, TrayReading& tr) {
        tr.temperatureC = Reading::invalid();
        tr.humidityPct = Reading::invalid();
        if (errors_[tray] < 0xFFFF) {
            ++errors_[tray];
        }
        if (errors_[tray] == kErrorsBeforeRecovery) {
            recoverBus();
            ready_[tray] = false;
        }
    }

    static bool plausible(float v, float lo, float hi) { return !isnan(v) && v >= lo && v <= hi; }

    /**
     * @brief Odblokowanie zawieszonej magistrali I2C.
     *
     * Slave przerwany w trakcie transmisji potrafi trzymac SDA w stanie niskim.
     * Wygenerowanie do 9 impulsow zegara + warunku STOP zwalnia linie.
     */
    static void recoverBus() {
        pinMode(GH_PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
        pinMode(GH_PIN_I2C_SDA, INPUT_PULLUP);
        for (uint8_t i = 0; i < 9; ++i) {
            digitalWrite(GH_PIN_I2C_SCL, LOW);
            delayMicroseconds(5);
            digitalWrite(GH_PIN_I2C_SCL, HIGH);
            delayMicroseconds(5);
            if (digitalRead(GH_PIN_I2C_SDA) == HIGH) {
                break;
            }
        }
        Wire.begin(GH_PIN_I2C_SDA, GH_PIN_I2C_SCL);
        Wire.setClock(100000);
    }

    // --- Czujniki poziomu wody ------------------------------------------
#if GH_LEVEL_SENSORS_DIRECT
    void initDirectLevelPins() {
        static const uint8_t pins[] = {GH_PIN_LEVEL_0, GH_PIN_LEVEL_1};
        for (uint8_t i = 0; i < trayCount_ && i < sizeof(pins); ++i) {
            pinMode(pins[i], INPUT_PULLUP);
        }
    }

    WaterLevel readLevelDirect(uint8_t tray) {
        static const uint8_t pins[] = {GH_PIN_LEVEL_0, GH_PIN_LEVEL_1};
        if (tray >= sizeof(pins)) {
            return WaterLevel::Unknown;
        }
        return digitalRead(pins[tray]) == GH_LEVEL_DRY_LEVEL ? WaterLevel::Dry : WaterLevel::Wet;
    }
#else
    void initExpander() {
        Wire.beginTransmission(GH_I2C_EXPANDER_ADDR);
        Wire.write(0xFF);  // quasi-dwukierunkowe wejscia: zapis jedynek
        expanderOk_ = (Wire.endTransmission() == 0);
    }

    uint8_t readExpander() {
        if (!expanderOk_) {
            initExpander();
            if (!expanderOk_) {
                return 0xFF;  // brak danych -> Unknown po dekodowaniu
            }
        }
        if (Wire.requestFrom(static_cast<uint8_t>(GH_I2C_EXPANDER_ADDR), static_cast<uint8_t>(1)) !=
            1) {
            expanderOk_ = false;
            expanderFail_ = true;
            return 0xFF;
        }
        expanderFail_ = false;
        return static_cast<uint8_t>(Wire.read());
    }

    WaterLevel decodeLevelBit(uint8_t bits, uint8_t tray) const {
        if (expanderFail_ || !expanderOk_) {
            return WaterLevel::Unknown;
        }
        const bool high = (bits & (1U << tray)) != 0;
        const bool dry = (GH_LEVEL_DRY_LEVEL == HIGH) ? high : !high;
        return dry ? WaterLevel::Dry : WaterLevel::Wet;
    }

    bool expanderOk_ = false;
    bool expanderFail_ = false;
#endif

    static constexpr uint16_t kErrorsBeforeRecovery = 3;

    Adafruit_SHT31 sensors_[kMaxTrays];
    bool ready_[kMaxTrays] = {false, false, false, false};
    uint16_t errors_[kMaxTrays] = {0, 0, 0, 0};
    uint8_t trayCount_;
};

}  // namespace gh
