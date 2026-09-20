# Współpraca przy projekcie

## Przygotowanie środowiska

```bash
pip install platformio
sudo apt-get install clang-format cppcheck   # narzędzia używane przez CI
pio test -e native                           # wszystko powinno przechodzić
```

## Zasada podstawowa: logika osobno od sprzętu

Nowa logika sterowania trafia do `lib/gh_domain/` i **nie może** zawierać
`#include <Arduino.h>`. Czas, odczyty i stan są przekazywane jako argumenty.
Dzięki temu każda decyzja regulatora daje się przetestować bez szklarni
i bez czekania w czasie rzeczywistym.

Kod dotykający sprzętu lub sieci należy do `src/drivers/` i `src/infra/`,
za interfejsem z `gh/Ports.h`.

## Wymagania wobec zmian

- **Testy** — każda zmiana logiki potrzebuje testu. Nowy plik testowy wymaga
  dopisania deklaracji i `RUN_TEST` w `test/test_domain/main.cpp`.
- **Formatowanie** — `clang-format -i` na zmienionych plikach; CI to sprawdza.
- **Zero ostrzeżeń** — kod kompiluje się czysto z `-Wall -Wextra`.
- **Komentarze wyjaśniają „dlaczego"**, nie „co". `// zwiększ licznik` jest
  bezwartościowy; `// wybieg wentylatora rozprowadza ciepło z mat po komorze`
  oszczędza następnej osobie pół godziny.

## Szczególna ostrożność

Ten kod steruje grzałkami i pompą w pomieszczeniu bez nadzoru. Przy zmianach
w `ClimateController`, `IrrigationController` i nadzorze bezpieczeństwa
`GreenhouseController` dopisz test scenariusza awaryjnego — nie tylko ścieżki
optymistycznej. Pytania kontrolne:

- Co się stanie, gdy czujnik przestanie odpowiadać w trakcie tej operacji?
- Co się stanie, gdy `millis()` przepełni się (co ok. 49 dni)?
- Czy ta zmiana pozwala pompie pracować dłużej, niż zakładaliśmy?

## Wiadomości commit

Format [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(irrigation): dobowy limit podlewania
fix(climate): wentylator nie wyłączał się w paśmie histerezy
docs(hardware): bilans zasilania 12 V
test(light): fotoperiod przechodzący przez północ
```

Typy: `feat`, `fix`, `docs`, `test`, `refactor`, `ci`, `build`, `chore`.

## Pull requesty

1. Gałąź od `develop`.
2. `pio test -e native` i `pio run -e nodemcuv2` muszą przechodzić lokalnie.
3. Opisz, co i **dlaczego** zmieniasz — jeśli zmiana dotyczy sprzętu, napisz,
   na jakiej konfiguracji ją sprawdziłeś.
4. Zmiany nastaw domyślnych wymagają uzasadnienia agronomicznego lub
   technicznego w opisie PR.

## Wydania

Wersjonowanie semantyczne. Tag `vX.Y.Z` na `main` uruchamia workflow wydania,
który buduje firmware z numerem wersji wkompilowanym w binarkę i publikuje
artefakty wraz z sumami kontrolnymi.
