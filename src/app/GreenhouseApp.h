// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>

#include "config/Pins.h"
#include "drivers/RelayBank.h"
#include "drivers/TraySensorBus.h"
#include "gh/GreenhouseController.h"
#include "gh/Version.h"
#include "infra/InfluxTelemetry.h"
#include "infra/NetworkService.h"
#include "infra/Settings.h"
#include "infra/SystemClock.h"
#include "infra/WebApi.h"

namespace gh {

/**
 * @brief Zlozenie calej aplikacji - jedyne miejsce, gdzie warstwy sie spotykaja.
 *
 * Petla glowna jest kooperacyjna i nieblokujaca: kazde zadanie ma wlasny
 * interwal, a `loop()` wraca szybko, zeby stos Wi-Fi ESP8266 i watchdog
 * dostaly czas procesora. Dlugie `delay()` w tego typu urzadzeniu konczy sie
 * resetem WDT - i wylaczeniem grzania w srodku nocy.
 */
class GreenhouseApp {
 public:
    GreenhouseApp()
        : sensors_(kDefaultTrayCount),
          controller_(config_),
          telemetry_(network_, clock_),
          netService_(network_),
          web_(controller_, config_, network_) {}

    void setup() {
        Serial.begin(115200);
        Serial.println();
        Serial.printf("[boot] smart-herb-greenhouse %s\n", firmwareVersion());

        pinMode(GH_PIN_STATUS_LED, OUTPUT);
        digitalWrite(GH_PIN_STATUS_LED, HIGH);  // LED ESP8266 jest aktywny stanem niskim

        // 1. Przekazniki jako pierwsze - gwarancja, ze nic sie nie zalaczy
        //    przypadkiem zanim reszta systemu wstanie.
        relays_.begin();

        // 2. Konfiguracja z pamieci trwalej.
        settings_.begin();
        if (!settings_.load(config_, network_)) {
            Serial.println("[cfg] brak zapisanej konfiguracji - uzywam domyslnej");
            applyBuildTimeDefaults();
            settings_.save(config_, network_);
        }
        controller_.setConfig(config_);

        // 3. Czujniki.
        sensors_.setTrayCount(config_.trayCount);
        if (!sensors_.begin()) {
            Serial.println("[i2c] UWAGA: zaden czujnik SHT-31 nie odpowiada");
        }

        // 4. Siec (nieblokujaco), czas, telemetria, API.
        netService_.onOtaStart_ = [this]() {
            Serial.println("[ota] start aktualizacji - wylaczam odbiorniki");
            relays_.allOff();
        };
        netService_.begin();
        clock_.begin(network_.timezone, network_.ntpServer1, network_.ntpServer2);
        telemetry_.begin();
        web_.begin([this]() -> const GreenhouseStatus& { return status_; },
                   [this](const GreenhouseConfig& c) { return settings_.save(c, network_); });

        Serial.printf("[boot] gotowe, tace: %u\n", static_cast<unsigned>(config_.trayCount));
    }

    void loop() {
        const uint32_t now = millis();

        netService_.loop(now);
        web_.loop();

        if (now - lastControl_ >= config_.controlIntervalMs) {
            lastControl_ = now;
            runControlCycle(now);
        }

        if (now - lastTelemetry_ >= config_.telemetryIntervalMs) {
            lastTelemetry_ = now;
            telemetry_.publish(status_, lastSnapshot_);
        } else if (now - lastFlush_ >= kFlushIntervalMs) {
            lastFlush_ = now;
            telemetry_.flush();  // dosylanie bufora offline
        }

        updateStatusLed(now);
        yield();
    }

 private:
    static constexpr uint8_t kDefaultTrayCount = 3;
    static constexpr uint32_t kFlushIntervalMs = 10000;

    void runControlCycle(uint32_t now) {
        lastSnapshot_ = sensors_.read();
        const WallClock wc = clock_.wallClock();
        status_ = controller_.update(lastSnapshot_, wc, now);
        relays_.apply(status_.command);

        if (status_.fault != lastReportedFault_) {
            lastReportedFault_ = status_.fault;
            Serial.printf("[fault] %s\n", toString(status_.fault));
        }
    }

    /**
     * @brief Sygnalizacja stanu wbudowana dioda - diagnostyka bez podlaczania laptopa.
     *   ciagle swiatlo   - praca normalna, siec OK
     *   wolne miganie    - brak Wi-Fi
     *   szybkie miganie  - usterka (czujniki / przegrzanie / pusty zbiornik)
     */
    void updateStatusLed(uint32_t now) {
        uint32_t period = 0;
        if (status_.fault != FaultCode::None) {
            period = 200;
        } else if (!netService_.connected()) {
            period = 1000;
        }

        if (period == 0) {
            digitalWrite(GH_PIN_STATUS_LED, LOW);  // LOW = swieci
            return;
        }
        if (now - lastBlink_ >= period) {
            lastBlink_ = now;
            ledOn_ = !ledOn_;
            digitalWrite(GH_PIN_STATUS_LED, ledOn_ ? LOW : HIGH);
        }
    }

    /// Wartosci wstrzykiwane przy kompilacji (secrets w CI / build flags).
    void applyBuildTimeDefaults() {
#ifdef GH_WIFI_SSID
        strncpy(network_.wifiSsid, GH_WIFI_SSID, sizeof(network_.wifiSsid) - 1);
#endif
#ifdef GH_WIFI_PASSWORD
        strncpy(network_.wifiPassword, GH_WIFI_PASSWORD, sizeof(network_.wifiPassword) - 1);
#endif
#ifdef GH_INFLUX_URL
        strncpy(network_.influxUrl, GH_INFLUX_URL, sizeof(network_.influxUrl) - 1);
#endif
#ifdef GH_INFLUX_TOKEN
        strncpy(network_.influxToken, GH_INFLUX_TOKEN, sizeof(network_.influxToken) - 1);
#endif
#ifdef GH_INFLUX_ORG
        strncpy(network_.influxOrg, GH_INFLUX_ORG, sizeof(network_.influxOrg) - 1);
#endif
    }

    // Kolejnosc pol odpowiada kolejnosci na liscie inicjalizacyjnej konstruktora.
    GreenhouseConfig config_;
    NetworkSettings network_;
    Settings settings_;
    RelayBank relays_;
    TraySensorBus sensors_;
    SystemClock clock_;
    GreenhouseController controller_;
    InfluxTelemetry telemetry_;
    NetworkService netService_;
    WebApi web_;

    SensorSnapshot lastSnapshot_;
    GreenhouseStatus status_;
    FaultCode lastReportedFault_ = FaultCode::None;

    uint32_t lastControl_ = 0;
    uint32_t lastTelemetry_ = 0;
    uint32_t lastFlush_ = 0;
    uint32_t lastBlink_ = 0;
    bool ledOn_ = false;
};

}  // namespace gh
