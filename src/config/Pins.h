// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>

/**
 * @file Pins.h
 * @brief Mapa wyprowadzen ESP8266MOD (ESP-12E/F) dla instalacji ze schematu.
 *
 * UWAGA PROJEKTOWA - ograniczenia ESP8266 wzgledem schematu:
 *
 *  1. Schemat pokazuje 3 czujniki SHT-31 na wspolnej magistrali. SHT-31 ma
 *     tylko dwa adresy I2C (0x44 / 0x45 - pin ADDR do GND albo VDD), wiec
 *     trzeciego czujnika nie da sie zaadresowac bezposrednio. Rozwiazanie:
 *     multiplekser I2C TCA9548A - kazda taca na osobnym kanale. Sterownik
 *     obsluguje tez wariant bez multipleksera (2 tace, rozne adresy)
 *     - patrz GH_USE_I2C_MUX.
 *
 *  2. ESP8266 ma jedno wejscie analogowe (A0) i bardzo malo wolnych GPIO.
 *     Trzy czujniki LM-393 podlaczamy wyjsciami cyfrowymi (D0 komparatora)
 *     do ekspandera PCF8574 na tej samej magistrali I2C - to oszczedza piny
 *     i eliminuje problem pinow GPIO0/GPIO2/GPIO15, ktore maja wymagania
 *     poziomow przy starcie. Wariant alternatywny (bezposrednio na GPIO)
 *     jest dostepny przez GH_LEVEL_SENSORS_DIRECT.
 *
 *  3. GPIO15 jest celowo pominiety dla przekaznikow: modul przekaznikowy ma
 *     wejscia aktywne stanem niskim i podciaga linie do 5 V przez opto -
 *     wysoki poziom na GPIO15 przy starcie uniemozliwia bootowanie ESP8266.
 */

// --- Magistrala I2C ------------------------------------------------------
#ifndef GH_PIN_I2C_SDA
#define GH_PIN_I2C_SDA 4  // D2
#endif
#ifndef GH_PIN_I2C_SCL
#define GH_PIN_I2C_SCL 5  // D1
#endif

// --- Modul 8 przekaznikow (wejscia aktywne stanem NISKIM) ---------------
#ifndef GH_PIN_RELAY_PUMP
#define GH_PIN_RELAY_PUMP 14  // D5  -> CH1 pump control
#endif
#ifndef GH_PIN_RELAY_FAN
#define GH_PIN_RELAY_FAN 12  // D6  -> CH2 fan control
#endif
#ifndef GH_PIN_RELAY_HEATER
#define GH_PIN_RELAY_HEATER 13  // D7  -> CH3 heat control
#endif
#ifndef GH_PIN_RELAY_LIGHT
#define GH_PIN_RELAY_LIGHT 16  // D0  -> CH4 light control
#endif

/// Poziom logiczny zalaczajacy przekaznik (typowy modul: LOW).
#ifndef GH_RELAY_ACTIVE_LEVEL
#define GH_RELAY_ACTIVE_LEVEL LOW
#endif

// --- Adresy I2C ----------------------------------------------------------
#ifndef GH_I2C_MUX_ADDR
#define GH_I2C_MUX_ADDR 0x70  // TCA9548A
#endif
#ifndef GH_I2C_EXPANDER_ADDR
#define GH_I2C_EXPANDER_ADDR 0x20  // PCF8574 - wejscia LM-393
#endif
#ifndef GH_SHT31_ADDR_PRIMARY
#define GH_SHT31_ADDR_PRIMARY 0x44
#endif
#ifndef GH_SHT31_ADDR_SECONDARY
#define GH_SHT31_ADDR_SECONDARY 0x45
#endif

// --- Warianty montazu ----------------------------------------------------
/// 1 = SHT-31 przez TCA9548A (3 tace), 0 = bezposrednio na I2C (maks. 2 tace).
#ifndef GH_USE_I2C_MUX
#define GH_USE_I2C_MUX 1
#endif

/// 1 = LM-393 bezposrednio na GPIO, 0 = przez ekspander PCF8574.
#ifndef GH_LEVEL_SENSORS_DIRECT
#define GH_LEVEL_SENSORS_DIRECT 0
#endif

#if GH_LEVEL_SENSORS_DIRECT
// Wariant bez ekspandera - dostepny realnie dla maks. 2 tac.
#ifndef GH_PIN_LEVEL_0
#define GH_PIN_LEVEL_0 0  // D3 - wymaga podciagniecia, stan WYSOKI przy starcie
#endif
#ifndef GH_PIN_LEVEL_1
#define GH_PIN_LEVEL_1 2  // D4 - jw.
#endif
#endif

/// Stan wyjscia komparatora LM-393 oznaczajacy "sucho".
/// Typowy modul: D0 = HIGH gdy poziom wody ponizej progu.
#ifndef GH_LEVEL_DRY_LEVEL
#define GH_LEVEL_DRY_LEVEL HIGH
#endif

// --- Pozostale -----------------------------------------------------------
#ifndef GH_PIN_STATUS_LED
#define GH_PIN_STATUS_LED LED_BUILTIN
#endif
