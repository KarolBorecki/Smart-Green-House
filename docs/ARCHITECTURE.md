# Architektura

## Założenie główne

Logika sterowania nie zależy od Arduino. Cała warstwa `gh::` (katalog
`lib/gh_domain/`) to czysty C++17 operujący na wartościach podawanych z zewnątrz:
odczytach czujników, czasie monotonicznym i czasie ściennym. Dzięki temu:

- testy jednostkowe działają na zwykłym kompilatorze w CI, bez sprzętu,
- scenariusz trwający w rzeczywistości dobę (limit podlewania) wykonuje się
  w milisekundach,
- wymiana czujnika czy modułu przekaźników nie dotyka logiki sterowania.

## Warstwy

```
┌───────────────────────────────────────────────┐
│ app/GreenhouseApp                             │  złożenie, pętla główna
├───────────────────────────────────────────────┤
│ drivers/            infra/                    │  adaptery sprzętu i sieci
│  RelayBank           NetworkService           │
│  TraySensorBus       InfluxTelemetry          │
│                      SystemClock, Settings    │
│                      WebApi                   │
├───────────────────────────────────────────────┤
│ gh::Ports  (IClock, ISensorBus,               │  interfejsy
│             IActuatorBank, ITelemetrySink)    │
├───────────────────────────────────────────────┤
│ gh:: domena — bez Arduino                     │  logika sterowania
│  GreenhouseController                         │
│  ClimateController, LightController,          │
│  IrrigationController, HysteresisSwitch       │
└───────────────────────────────────────────────┘
```

Zależności idą wyłącznie w dół. Domena nie wie o istnieniu I²C, Wi-Fi ani InfluxDB.

## Cykl sterowania

`GreenhouseApp::loop()` jest nieblokująca — każde zadanie ma własny interwał,
a funkcja szybko wraca, żeby stos Wi-Fi i watchdog dostały czas procesora.
Długi `delay()` w takim urządzeniu kończy się resetem watchdoga, czyli
wyłączeniem grzania w środku nocy.

Co 5 s (`controlIntervalMs`):

1. **Odczyt czujników** — `TraySensorBus::read()` zwraca `SensorSnapshot`.
   Każdy odczyt jest walidowany (NaN, zakres fizyczny); uszkodzony czujnik
   zgłasza `valid == false`, nigdy wartość domyślną.
2. **Agregacja** — średnia wyłącznie ze sprawnych czujników plus minimum
   i maksimum. Maksimum ma znaczenie: lokalne przegrzanie nad matą grzewczą
   nie jest widoczne w średniej.
3. **Fotoperiod** — wyznacza fazę dnia, a ta wybiera nastawę temperatury.
4. **Klimat i nawadnianie** — niezależne regulatory.
5. **Nadzór bezpieczeństwa** — może nadpisać każde wyjście.
6. **Ręczne nadpisania operatora** — nadrzędne wobec automatyki, ale nie wobec
   blokad bezpieczeństwa.
7. **Zapis na przekaźniki** — tylko rzeczywiste zmiany stanu.

## Decyzje projektowe i ich uzasadnienie

### Histereza plus minimalny czas stanu

Sama histereza nie wystarcza. Przy skokowej zmianie odczytu (otwarcie komory,
podmuch z wentylatora) przekaźnik przełączałby się wielokrotnie w ciągu minuty.
`HysteresisSwitch` dokłada minimalny czas załączenia i wyłączenia — domyślnie
po 60 s dla mat grzewczych. Pierwsze wywołanie po starcie jest zwolnione
z tego ograniczenia, inaczej grzanie ruszałoby dopiero minutę po restarcie.

### Nawadnianie impulsowe, nie „do mokrego"

Naturalne rozwiązanie — trzymać pompę włączoną aż czujnik zgłosi „mokro" — jest
błędne, bo woda przesiąka podłożem przez minuty, a nie sekundy. Pompa zalałaby
tacę wielokrotnie. Stąd maszyna stanów:

```
Idle ──(≥ N suchych tac, minął odstęp, limit dobowy OK)──► Pulsing (8 s)
                                                                 │
Idle ◄──(ocena skuteczności)── Soaking (5 min) ◄─────────────────┘
  │
  └──(3 nieskuteczne impulsy)──► Fault: pusty zbiornik
```

Po każdym cyklu sprawdzana jest skuteczność: jeśli liczba suchych tac nie
spadła, licznik nieskutecznych impulsów rośnie. Trzy z rzędu oznaczają pusty
zbiornik — pompa zostaje zablokowana do ręcznego resetu. Praca na sucho to
najszybszy sposób na zniszczenie pompy.

### Czujnik bez komunikacji nie uruchamia pompy

`WaterLevel::Unknown` nigdy nie jest liczony jako „sucho". Awaria magistrali
I²C nie może skutkować zalaniem uprawy.

### Tryb bezpieczny

| Warunek | Reakcja |
|---|---|
| Maksimum z czujników ≥ 38 °C | grzanie off, **lampy off** (też grzeją), wentylator on |
| Brak poprawnych odczytów > 3 min | grzanie off, pompa off, wentylator wg konfiguracji |
| Pusty zbiornik | pompa zablokowana do ręcznego resetu |
| Przekroczony limit dobowy | pompa zablokowana do następnej doby |

Blokady bezpieczeństwa są nadrzędne wobec ręcznych nadpisań: operator nie włączy
grzania w przegrzanej komorze.

### Ręczne nadpisania zawsze z czasem wygaśnięcia

API nie pozwala ustawić nadpisania bezterminowego (limit 6 h, domyślnie 15 min).
Pompa włączona „na chwilę" z telefonu i zapomniana to najprostszy sposób na
zalanie uprawy.

### Bufor telemetrii

Rekordy trafiają do pierścienia 24 pozycji w RAM i są dosyłane po odzyskaniu
łączności. Bez tego każda przerwa w Wi-Fi robiłaby dziurę w serii czasowej —
a ciągłość tej serii jest jedynym powodem, dla którego InfluxDB tu jest.
Po serii nieudanych prób najstarszy rekord jest porzucany, żeby bufor nie
zablokował się na niedziałającym endpoincie.

### Zapis konfiguracji przez plik tymczasowy

`Settings::save()` pisze do `/config.tmp` i dopiero potem podmienia `/config.json`.
Zanik zasilania w trakcie zapisu nie zostawia uszkodzonej konfiguracji.

### Stan przekaźników przed `pinMode`

`RelayBank::begin()` ustawia poziom nieaktywny **zanim** przełączy pin w tryb
wyjścia. Odwrotna kolejność powoduje krótkie załączenie wszystkich odbiorników
przy starcie — łącznie z pompą i grzałkami.

## Testy

52 testy jednostkowe w `test/test_domain/`, uruchamiane w CI przed każdą
kompilacją firmware. Pokrywają m.in.:

- histerezę, minimalny czas stanu i ochronę przed „klekotaniem" przekaźnika,
- fotoperiod z przejściem przez północ i tryb awaryjny bez NTP,
- pełną maszynę stanów nawadniania wraz z limitami i detekcją suchobiegu,
- agregację przy uszkodzonym czujniku,
- priorytet bezpieczeństwa nad ręcznymi nadpisaniami,
- format i escaping rekordów InfluxDB.
