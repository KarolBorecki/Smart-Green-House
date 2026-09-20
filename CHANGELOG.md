# Changelog

Format wg [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/),
wersjonowanie wg [SemVer](https://semver.org/lang/pl/).

## [Unreleased]

## [1.0.0] — 2026-09-20

Pierwsze wydanie sterownika szklarni ziołowej.

### Dodane

- **Regulacja klimatu** — maty grzewcze z histerezą i minimalnym czasem
  załączenia, osobne nastawy dzień/noc, wentylacja sterowana przegrzaniem,
  wilgotnością, cyklem wymiany powietrza i wybiegiem po grzaniu.
- **Fotoperiod** — okno świetlne w czasie lokalnym z obsługą zmiany czasu,
  poprawne przejście przez północ, tryb awaryjny przy braku NTP.
- **Nawadnianie impulsowe** — pompa 240 l/h, przerwa na wsiąkanie, minimalny
  odstęp między cyklami, dobowy limit impulsów, detekcja pustego zbiornika.
- **Nadzór bezpieczeństwa** — tryb awaryjny przy przegrzaniu (grzanie i lampy
  off, wentylator on) oraz przy utracie czujników; watchdog sprzętowy 8 s.
- **Telemetria InfluxDB 2.x** z buforem offline na 24 rekordy.
- **REST API i panel WWW** — podgląd stanu, zmiana nastaw z walidacją, ręczne
  nadpisania z obowiązkowym czasem wygaśnięcia, kasowanie blokad.
- **Aktualizacja OTA** z wyłączeniem odbiorników przed wgraniem obrazu.
- **Obsługa 3 tac** przez multiplekser I²C TCA9548A i ekspander PCF8574;
  wariant `nomux` dla instalacji z dwoma tacami.
- **52 testy jednostkowe** logiki sterowania, uruchamiane w CI bez sprzętu.
- **CI/CD** — testy, kompilacja dwóch wariantów, cppcheck, clang-format,
  raport zużycia pamięci, automatyczne wydania z sumami kontrolnymi.

[Unreleased]: https://github.com/OWNER/smart-herb-greenhouse/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/OWNER/smart-herb-greenhouse/releases/tag/v1.0.0
