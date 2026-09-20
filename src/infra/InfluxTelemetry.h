// SPDX-License-Identifier: MIT
#pragma once

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

#include "gh/LineProtocol.h"
#include "gh/Ports.h"
#include "infra/Settings.h"
#include "infra/SystemClock.h"

namespace gh {

/**
 * @brief Wysylka telemetrii do InfluxDB 2.x (API /api/v2/write).
 *
 * Bufor offline: gdy sklada sie awaria Wi-Fi albo bazy, rekordy trafiaja do
 * pierscienia w RAM i sa dosylane po odzyskaniu lacznosci. Bez tego kazda
 * przerwa w sieci robilaby dziure w danych pomiarowych - a to wlasnie ciaglosc
 * serii czasowej jest jedynym powodem, dla ktorego InfluxDB tu jest.
 *
 * Rozmiar bufora jest celowo maly (ESP8266 ma ~40 kB wolnego RAM przy WiFi+TLS):
 * dobierany tak, aby przetrwac kilkunastominutowa przerwe przy interwale 30 s.
 */
class InfluxTelemetry : public ITelemetrySink {
 public:
    static constexpr size_t kBufferSlots = 24;
    static constexpr size_t kMaxLineLen = 220;

    InfluxTelemetry(const NetworkSettings& net, const SystemClock& clock)
        : net_(net), clock_(clock), lp_(net.deviceId) {}

    bool begin() {
        http_.setReuse(true);
        http_.setTimeout(kHttpTimeoutMs);
        configured_ = net_.influxUrl[0] != '\0' && net_.influxToken[0] != '\0';
        return configured_;
    }

    bool publish(const GreenhouseStatus& status, const SensorSnapshot& snap) override {
        const uint64_t ts = clock_.epochNanos();

        enqueue(lp_.climateLine(status), ts);
        enqueue(lp_.actuatorLine(status), ts);
        enqueue(lp_.systemLine(status, ESP.getFreeHeap(), WiFi.RSSI()), ts);
        for (size_t i = 0; i < snap.trayCount; ++i) {
            enqueue(lp_.trayLine(i, snap.trays[i]), ts);
        }
        return flush();
    }

    /// Proba wyslania zawartosci bufora; bezpieczna do wolania w petli glownej.
    bool flush() {
        if (!configured_ || count_ == 0) {
            return count_ == 0;
        }
        if (WiFi.status() != WL_CONNECTED) {
            healthy_ = false;
            return false;
        }

        String payload;
        payload.reserve(count_ * 96);
        size_t sending = 0;
        for (size_t i = 0; i < count_ && i < kMaxLinesPerRequest; ++i) {
            const size_t idx = (head_ + i) % kBufferSlots;
            payload += buffer_[idx];
            payload += '\n';
            ++sending;
        }

        const int code = post(payload);
        if (code == 204 || code == 200) {
            head_ = (head_ + sending) % kBufferSlots;
            count_ -= sending;
            healthy_ = true;
            consecutiveFailures_ = 0;
            return true;
        }

        healthy_ = false;
        lastHttpCode_ = code;
        if (++consecutiveFailures_ > kFailuresBeforeDrop && count_ > 0) {
            // Chronimy najnowsze dane: porzucamy najstarszy rekord zamiast
            // blokowac bufor w nieskonczonosc na niedzialajacym endpointcie.
            head_ = (head_ + 1) % kBufferSlots;
            --count_;
        }
        return false;
    }

    bool healthy() const override { return healthy_; }
    int lastHttpCode() const { return lastHttpCode_; }
    size_t pending() const { return count_; }

 private:
    static constexpr uint16_t kHttpTimeoutMs = 4000;
    static constexpr size_t kMaxLinesPerRequest = 12;
    static constexpr uint8_t kFailuresBeforeDrop = 5;

    void enqueue(const std::string& line, uint64_t timestampNs) {
        if (line.empty()) {
            return;
        }
        String entry(line.c_str());
        if (timestampNs != 0) {
            entry += ' ';
            entry += u64ToString(timestampNs);
        }
        if (entry.length() > kMaxLineLen) {
            return;  // rekord niespodziewanie dlugi - nie ryzykujemy fragmentacji sterty
        }

        const size_t tail = (head_ + count_) % kBufferSlots;
        buffer_[tail] = entry;
        if (count_ == kBufferSlots) {
            head_ = (head_ + 1) % kBufferSlots;  // nadpisujemy najstarszy
        } else {
            ++count_;
        }
    }

    int post(const String& payload) {
        String url(net_.influxUrl);
        url += "/api/v2/write?org=";
        url += net_.influxOrg;
        url += "&bucket=";
        url += net_.influxBucket;
        url += "&precision=ns";

        WiFiClient client;
        if (!http_.begin(client, url)) {
            return -1;
        }
        String auth("Token ");
        auth += net_.influxToken;
        http_.addHeader("Authorization", auth);
        http_.addHeader("Content-Type", "text/plain; charset=utf-8");
        const int code = http_.POST(payload);
        http_.end();
        return code;
    }

    static String u64ToString(uint64_t v) {
        char buf[21];
        buf[20] = '\0';
        int i = 20;
        do {
            buf[--i] = static_cast<char>('0' + (v % 10));
            v /= 10;
        } while (v != 0 && i > 0);
        return String(&buf[i]);
    }

    const NetworkSettings& net_;
    const SystemClock& clock_;
    LineProtocol lp_;
    HTTPClient http_;
    String buffer_[kBufferSlots];
    size_t head_ = 0;
    size_t count_ = 0;
    uint8_t consecutiveFailures_ = 0;
    int lastHttpCode_ = 0;
    bool healthy_ = false;
    bool configured_ = false;
};

}  // namespace gh
