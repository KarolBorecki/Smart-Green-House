# Instalacja sprzętowa

## Spis elementów

| Element | Sztuk | Uwagi |
|---|---|---|
| ESP8266MOD (ESP-12E / NodeMCU v2) | 1 | sterownik |
| Moduł 8 przekaźników | 1 | wykorzystane kanały 1–4, wejścia aktywne stanem niskim |
| SHT-31 | 3 | temperatura i wilgotność, po jednym na tacę |
| LM-393 (czujnik poziomu wody) | 3 | wykorzystywane wyjście cyfrowe D0 |
| TCA9548A | 1 | multiplekser I²C — konieczny przy 3 czujnikach SHT-31 |
| PCF8574 | 1 | ekspander I/O dla wyjść komparatorów LM-393 |
| Pompa 240 l/h | 1 | 12 V |
| Wentylator 12 V | 1 | |
| Mata grzewcza 12 W | 2 | wspólny kanał przekaźnika |
| Lampa uprawowa | 2 | wspólny kanał przekaźnika |
| Zasilacz 230 V AC → 12 V DC | 1 | z zapasem mocy (patrz bilans niżej) |
| Przetwornica 12 V → 5 V DC | 1 | zasilanie ESP8266 i modułu przekaźników |

## Mapa wyprowadzeń ESP8266

| Funkcja | GPIO | Pin NodeMCU | Uwagi |
|---|---|---|---|
| I²C SDA | 4 | D2 | wspólna magistrala: TCA9548A + PCF8574 |
| I²C SCL | 5 | D1 | |
| Przekaźnik CH1 — pompa | 14 | D5 | |
| Przekaźnik CH2 — wentylator | 12 | D6 | |
| Przekaźnik CH3 — maty grzewcze | 13 | D7 | |
| Przekaźnik CH4 — lampy | 16 | D0 | brak przerwań, ale jako wyjście działa poprawnie |
| Dioda stanu | 2 | D4 | wbudowana, aktywna stanem niskim |

**GPIO15 jest celowo pominięty.** Wejścia modułu przekaźnikowego są podciągnięte
do 5 V; stan wysoki na GPIO15 podczas startu blokuje bootowanie ESP8266.

## Dlaczego multiplekser i ekspander

**TCA9548A (adres 0x70).** SHT-31 udostępnia tylko dwa adresy I²C — 0x44 (ADDR do
GND) i 0x45 (ADDR do VDD). Trzy czujniki na jednej magistrali nie są więc możliwe
bez multipleksera. Każda taca dostaje własny kanał (0, 1, 2), a czujniki mogą mieć
identyczny adres 0x44.

**PCF8574 (adres 0x20).** Trzy wyjścia cyfrowe LM-393 wymagałyby trzech GPIO, a po
przydzieleniu I²C i czterech przekaźników zostają tylko piny z wymaganiami
poziomów przy starcie (GPIO0, GPIO2, GPIO15). Czujnik zgłaszający stan „sucho"
na GPIO0 uniemożliwiłby uruchomienie układu. Ekspander rozwiązuje to jedną kostką
na już istniejącej magistrali.

Bity ekspandera: P0 → taca 0, P1 → taca 1, P2 → taca 2.

## Podłączenie czujników

```
ESP8266 ──┬── SDA (D2) ──┬── TCA9548A SDA ──┬── kanał 0 → SHT-31 taca 0 (0x44)
          │              │                  ├── kanał 1 → SHT-31 taca 1 (0x44)
          │              │                  └── kanał 2 → SHT-31 taca 2 (0x44)
          │              └── PCF8574 SDA ──── P0..P2 → LM-393 D0 (tace 0..2)
          └── SCL (D1) ───── (wspólnie jw.)
```

Rezystory podciągające 4,7 kΩ na SDA i SCL do 3,3 V — zwykle są już na modułach
TCA9548A; przy dłuższych przewodach w szklarni warto je dodać przy sterowniku.

Wilgotność w komorze jest wysoka, dlatego płytki czujników powinny mieć
zabezpieczone złącza (lakier lub koszulka termokurczliwa na przewodach),
a same moduły LM-393 montowane poza strefą zachlapania — w wodzie jest tylko
sonda.

## Bilans zasilania 12 V

| Odbiornik | Pobór | Uwagi |
|---|---|---|
| 2 × mata grzewcza | 24 W (2,0 A) | najdłużej pracujący odbiornik |
| Pompa 240 l/h | ok. 5 W (0,4 A) | praca impulsowa |
| Wentylator | ok. 2 W (0,17 A) | |
| 2 × lampa uprawowa | zależnie od modelu | doliczyć z karty katalogowej |
| ESP8266 + przekaźniki (przez przetwornicę 5 V) | ok. 3 W | szczyt ESP8266 przy nadawaniu: 350 mA |

Zasilacz dobierz z ok. 30 % zapasu ponad sumę. Firmware rozsuwa załączenia
przekaźników o 120 ms, żeby nie sumować udarów prądowych, ale nie zastąpi to
zasilacza o właściwej mocy.

## Uruchomienie — kolejność sprawdzeń

1. **Bez odbiorników 12 V.** Wgraj firmware, sprawdź logi na 115200 baud —
   powinny wylistować wykryte czujniki.
2. **Skanowanie I²C.** W logach musi pojawić się 0x70 (mux) i 0x20 (ekspander).
   Brak oznacza błąd okablowania magistrali.
3. **Sprawdzenie czujników.** `curl http://herb-greenhouse.local/api/status` —
   pole `sensors_ok` powinno wynosić tyle, ile tac.
4. **Test przekaźników po kolei**, z odłączonymi odbiornikami:
   ```bash
   curl -X POST .../api/override -d '{"actuator":"fan","value":true,"ttl_ms":5000}'
   ```
   Słyszalne kliknięcie i dioda na module potwierdzają właściwy kanał.
5. **Kalibracja LM-393.** Potencjometr na module ustaw tak, aby dioda progu
   zmieniała stan przy pożądanej wilgotności podłoża. Stan czujnika sprawdzisz
   w polu `dry_trays`.
6. **Dopiero na końcu podłącz odbiorniki 12 V** i obserwuj pierwszy pełny cykl
   podlewania — szczególnie czy woda wraca do zbiornika i czy pompa nie pracuje
   na sucho.
