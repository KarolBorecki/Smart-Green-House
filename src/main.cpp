// SPDX-License-Identifier: MIT
/**
 * @file main.cpp
 * @brief Punkt wejscia firmware sterownika szklarni ziolowej (ESP8266MOD).
 *
 * Cala logika mieszka w `gh::GreenhouseApp`; tutaj zostaje wylacznie
 * kontrakt Arduino (setup/loop) oraz konfiguracja watchdoga.
 */
#include <Arduino.h>

#include "app/GreenhouseApp.h"

namespace {
gh::GreenhouseApp app;
}  // namespace

void setup() {
    // Watchdog sprzetowy: jesli petla glowna zablokuje sie na dluzej niz 8 s,
    // urzadzenie sie zrestartuje, a przekazniki wroca do stanu nieaktywnego.
    ESP.wdtDisable();
    ESP.wdtEnable(8000);
    app.setup();
}

void loop() {
    app.loop();
    ESP.wdtFeed();
}
