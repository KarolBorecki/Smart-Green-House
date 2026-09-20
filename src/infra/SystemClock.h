// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>
#include <time.h>

#include "gh/Ports.h"

namespace gh {

/**
 * @brief Zegar systemowy: monotoniczny `millis()` + czas lokalny z NTP.
 *
 * Strefa czasowa jest podawana w formacie POSIX TZ, dzieki czemu zmiana czasu
 * letni/zimowy dzieje sie automatycznie - istotne, bo fotoperiod jest
 * definiowany w czasie lokalnym i inaczej "przeskakiwalby" dwa razy w roku.
 * Dla Polski: "CET-1CEST,M3.5.0,M10.5.0/3".
 */
class SystemClock : public IClock {
 public:
    void begin(const char* tz, const char* ntp1, const char* ntp2) { configTime(tz, ntp1, ntp2); }

    Millis millisNow() const override { return millis(); }

    WallClock wallClock() const override {
        WallClock wc;
        const time_t now = time(nullptr);
        if (now < kMinValidEpoch) {
            return wc;  // brak synchronizacji
        }
        struct tm lt;
        localtime_r(&now, &lt);
        wc.minuteOfDay = static_cast<uint16_t>(lt.tm_hour * 60 + lt.tm_min);
        wc.dayNumber = static_cast<uint32_t>(now / 86400L);
        wc.valid = true;
        return wc;
    }

    bool synced() const { return time(nullptr) >= kMinValidEpoch; }

    /// Znacznik czasu w nanosekundach dla InfluxDB (0 gdy brak synchronizacji).
    uint64_t epochNanos() const {
        const time_t now = time(nullptr);
        if (now < kMinValidEpoch) {
            return 0;
        }
        return static_cast<uint64_t>(now) * 1000000000ULL;
    }

 private:
    // 2021-01-01 - wartosc ponizej oznacza, ze zegar nie zostal ustawiony.
    static constexpr time_t kMinValidEpoch = 1609459200L;
};

}  // namespace gh
