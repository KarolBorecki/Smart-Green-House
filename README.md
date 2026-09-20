# Smart Herb Greenhouse

Firmware sterownika zautomatyzowanej szklarni ziołowej (komora 140 dm³) opartego na ESP8266MOD.
Steruje klimatem, fotoperiodem i nawadnianiem, a dane pomiarowe zapisuje w InfluxDB.

[![CI](https://github.com/OWNER/smart-herb-greenhouse/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/smart-herb-greenhouse/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## Co robi sterownik

| Funkcja | Realizacja |
|---|---|
| Temperatura | Maty grzewcze 2 × 12 W, regulacja z histerezą, osobne nastawy dzień/noc |
| Wentylacja | Wentylator 12 V — przegrzanie, nadmiar wilgoci, cykliczna wymiana powietrza, wybieg po grzaniu |
| Oświetlenie | Fotoperiod w czasie lokalnym (NTP + strefa czasowa), tryb awaryjny bez sieci |
| Nawadnianie | Pompa 240 l/h, podlewanie impulsowe z przerwą na wsiąkanie, limity dobowe, detekcja pustego zbiornika |
| Telemetria | InfluxDB 2.x (line protocol) z buforem offline na czas awarii sieci |
| Obsługa | Panel WWW + REST API, aktualizacja OTA, sygnalizacja stanu diodą |
| Bezpieczeństwo | Tryb awaryjny przy przegrzaniu i utracie czujników, watchdog, TTL na ręcznych nadpisaniach |

## Architektura

Kod jest podzielony tak, żeby logika sterowania dała się testować bez sprzętu —
cała warstwa `gh::` to czysty C++17 bez Arduino, a testy jednostkowe działają
w CI na zwykłym kompilatorze.

```
lib/gh_domain/      logika sterowania (czysty C++, bez Arduino)
  Hysteresis.h        przełącznik z histerezą i minimalnym czasem stanu
  ClimateController   grzanie + wentylacja
  LightController     fotoperiod
  IrrigationController maszyna stanów nawadniania
  GreenhouseController spięcie całości + nadzór bezpieczeństwa
  LineProtocol.h      format rekordów InfluxDB
  Ports.h             interfejsy (IClock, ISensorBus, IActuatorBank, ITelemetrySink)

src/
  config/Pins.h       mapa wyprowadzeń i warianty montażowe
  drivers/            RelayBank, TraySensorBus (SHT-31 + LM-393)
  infra/              WiFi/OTA, NTP, InfluxDB, REST API, ustawienia w LittleFS
  app/                GreenhouseApp — złożenie warstw
  main.cpp            setup()/loop() + watchdog

test/test_domain/   52 testy jednostkowe (Unity, środowisko native)
data/               panel WWW wgrywany do LittleFS
```

Szczegóły: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Uwagi do schematu — dwa ograniczenia ESP8266

Schemat zakłada trzy czujniki SHT-31 i trzy LM-393 na wspólnej magistrali.
W praktyce:

1. **SHT-31 ma tylko dwa adresy I²C** (0x44 i 0x45), więc trzeciego czujnika nie da
   się zaadresować bezpośrednio. Firmware zakłada multiplekser **TCA9548A** — każda
   taca na osobnym kanale. Wariant bez multipleksera (2 tace) jest dostępny jako
   środowisko `nodemcuv2-nomux`.
2. **ESP8266 ma jedno wejście analogowe i mało wolnych GPIO.** Wyjścia komparatorów
   LM-393 są czytane przez ekspander **PCF8574** na tej samej magistrali I²C.
   Uwalnia to piny GPIO0/GPIO2/GPIO15, które mają wymagania poziomów przy starcie —
   czujnik zgłaszający „sucho" na GPIO0 uniemożliwiłby bootowanie układu.

Pełna lista połączeń: [docs/HARDWARE.md](docs/HARDWARE.md).

## Szybki start

```bash
git clone https://github.com/OWNER/smart-herb-greenhouse.git
cd smart-herb-greenhouse
pip install platformio

pio test -e native          # testy logiki, bez sprzętu
pio run -e nodemcuv2        # kompilacja firmware
pio run -e nodemcuv2 -t upload      # wgranie przez USB
pio run -e nodemcuv2 -t uploadfs    # wgranie panelu WWW do LittleFS
pio device monitor                  # podgląd logów
```

Po pierwszym uruchomieniu sterownik tworzy `/config.json` z wartościami domyślnymi.
Dane Wi-Fi i InfluxDB można podać przy kompilacji:

```bash
pio run -e nodemcuv2 \
  -D GH_WIFI_SSID='"moja-siec"' \
  -D GH_WIFI_PASSWORD='"haslo"' \
  -D GH_INFLUX_URL='"http://192.168.1.10:8086"' \
  -D GH_INFLUX_ORG='"dom"' \
  -D GH_INFLUX_TOKEN='"..."'
```

albo później przez panel WWW pod `http://herb-greenhouse.local`.

## Nastawy domyślne

| Parametr | Wartość | Uzasadnienie |
|---|---|---|
| Temperatura dzień / noc | 22 °C / 18 °C | typowy zakres dla ziół liściowych |
| Histereza | ±0,7 °C | kompromis między stabilnością a liczbą załączeń przekaźnika |
| Limit wentylacji | 28 °C | powyżej bazylia i mięta zaczynają więdnąć |
| Limit krytyczny | 38 °C | twarde odcięcie grzania i lamp |
| Maks. wilgotność | 70 % | powyżej rośnie ryzyko pleśni |
| Fotoperiod | 06:00–22:00 (16 h) | zioła liściowe |
| Impuls podlewania | 8 s co min. 30 min | ok. 0,5 l na impuls przy 240 l/h |
| Limit dobowy | 12 impulsów | zabezpieczenie przed zalaniem |

Wszystkie parametry są edytowalne przez API i zapisywane w LittleFS —
zmiana nastaw nie wymaga rekompilacji.

## API

```bash
curl http://herb-greenhouse.local/api/status
curl -X POST http://herb-greenhouse.local/api/override \
     -H 'Content-Type: application/json' \
     -d '{"actuator":"pump","value":true,"ttl_ms":60000}'
```

Pełna dokumentacja: [docs/API.md](docs/API.md).

## CI/CD

| Workflow | Kiedy | Co robi |
|---|---|---|
| `ci.yml` | push / PR | testy jednostkowe → kompilacja dwóch wariantów → cppcheck + clang-format → raport zużycia pamięci |
| `release.yml` | tag `v*.*.*` | testy, build z numerem wersji wkompilowanym w firmware, sumy SHA256, publikacja wydania |
| `library-updates.yml` | co tydzień | build z najnowszymi bibliotekami, zgłoszenie issue przy regresji |
| `dependabot.yml` | co tydzień | aktualizacje akcji GitHub |

## Licencja

MIT — zobacz [LICENSE](LICENSE).
