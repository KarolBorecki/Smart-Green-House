// SPDX-License-Identifier: MIT
#pragma once

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "infra/Settings.h"

namespace gh {

/**
 * @brief Lacznosc Wi-Fi + mDNS + OTA, z nieblokujacym ponawianiem polaczenia.
 *
 * Zalozenie projektowe: brak sieci NIE MOZE zatrzymac sterowania. Szklarnia ma
 * dalej grzac, swiecic i podlewac, a telemetria jest funkcja dodatkowa.
 * Dlatego nigdzie nie ma petli `while (WiFi.status() != WL_CONNECTED) delay()`,
 * a ponawianie polaczenia odbywa sie z narastajacym odstepem.
 */
class NetworkService {
 public:
    explicit NetworkService(const NetworkSettings& net) : net_(net) {}

    void begin() {
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);  // stabilniejsze HTTP przy zasilaniu sieciowym
        WiFi.hostname(net_.hostname);
        connect();
        setupOta();
    }

    /// Wolane w kazdej iteracji petli glownej - nigdy nie blokuje.
    void loop(uint32_t now) {
        ArduinoOTA.handle();
        MDNS.update();

        if (WiFi.status() == WL_CONNECTED) {
            if (!wasConnected_) {
                wasConnected_ = true;
                backoffMs_ = kMinBackoffMs;
                MDNS.begin(net_.hostname);
                MDNS.addService("http", "tcp", 80);
            }
            return;
        }

        wasConnected_ = false;
        if (now - lastAttempt_ < backoffMs_) {
            return;
        }
        connect();
        lastAttempt_ = now;
        backoffMs_ = backoffMs_ * 2 > kMaxBackoffMs ? kMaxBackoffMs : backoffMs_ * 2;
    }

    bool connected() const { return WiFi.status() == WL_CONNECTED; }
    String ip() const { return WiFi.localIP().toString(); }

 private:
    static constexpr uint32_t kMinBackoffMs = 5000;
    static constexpr uint32_t kMaxBackoffMs = 120000;

    void connect() {
        if (net_.wifiSsid[0] == '\0') {
            return;
        }
        WiFi.begin(net_.wifiSsid, net_.wifiPassword);
    }

    void setupOta() {
        ArduinoOTA.setHostname(net_.hostname);
        if (net_.otaPassword[0] != '\0') {
            ArduinoOTA.setPassword(net_.otaPassword);
        }
        ArduinoOTA.onStart([this]() {
            if (onOtaStart_) {
                onOtaStart_();
            }
        });
        ArduinoOTA.begin();
    }

    const NetworkSettings& net_;
    uint32_t lastAttempt_ = 0;
    uint32_t backoffMs_ = kMinBackoffMs;
    bool wasConnected_ = false;

 public:
    /// Callback wywolywany przed startem aktualizacji OTA (wylaczenie przekaznikow).
    std::function<void()> onOtaStart_;
};

}  // namespace gh
