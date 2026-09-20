# Telemetria — InfluxDB

Sterownik wysyła dane co 30 s (`telemetryIntervalMs`) do InfluxDB 2.x przez
`POST /api/v2/write` w formacie line protocol.

## Przygotowanie bazy

```bash
influx bucket create --name greenhouse --org dom --retention 365d
influx auth create --org dom --write-bucket greenhouse --description "herb-01"
```

Token wpisz w konfiguracji sterownika (panel WWW lub flaga `-D GH_INFLUX_TOKEN`).

## Rekordy

### `climate` — warunki uśrednione w komorze

| Pole | Typ | Opis |
|---|---|---|
| `temperature` | float | średnia ze sprawnych czujników |
| `temperature_min` / `temperature_max` | float | skrajne odczyty — ujawniają lokalne przegrzania |
| `humidity` | float | wilgotność względna [%] |
| `target_temperature` | float | aktualna nastawa (inna w dzień i w nocy) |
| `sensors_ok` | int | liczba sprawnych czujników |
| `dry_trays` | int | liczba tac zgłaszających „sucho" |

### `tray` — dane z pojedynczej tacy (tag `tray` = 0, 1, 2)

`temperature`, `humidity`, `water_dry` (bool), `sensor_ok` (bool).

### `actuators` — stany wyjść

`pump`, `fan`, `heater`, `light` (bool), `irrigation_state` (string),
`pulses_today` (int), `fan_reason` (string).

### `system` — zdrowie sterownika

`uptime_s`, `free_heap`, `rssi` (int), `fault` (string), `safe_mode`,
`ntp_sync` (bool).

Wszystkie rekordy mają tag `device` (domyślnie `herb-01`), co pozwala zbierać
dane z kilku szklarni do jednego bucketu.

## Przydatne zapytania (Flux)

**Temperatura z nastawą — podstawowy wykres:**

```flux
from(bucket: "greenhouse")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "climate")
  |> filter(fn: (r) => r._field == "temperature" or r._field == "target_temperature")
  |> aggregateWindow(every: 5m, fn: mean)
```

**Czas pracy grzania na dobę** — rośnie zimą, a skokowy wzrost przy stałej
pogodzie sygnalizuje nieszczelność komory:

```flux
from(bucket: "greenhouse")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "actuators" and r._field == "heater")
  |> map(fn: (r) => ({ r with _value: if r._value then 1.0 else 0.0 }))
  |> aggregateWindow(every: 1d, fn: mean)
  |> map(fn: (r) => ({ r with _value: r._value * 24.0 }))
```

**Zużycie wody** — liczba impulsów × 8 s × 240 l/h ≈ 0,53 l na impuls:

```flux
from(bucket: "greenhouse")
  |> range(start: -30d)
  |> filter(fn: (r) => r._measurement == "actuators" and r._field == "pulses_today")
  |> aggregateWindow(every: 1d, fn: max)
  |> map(fn: (r) => ({ r with _value: float(v: r._value) * 0.53 }))
```

**Rozjazd między tacami** — różnica powyżej 2–3 °C oznacza, że maty grzewcze
lub wentylator są źle rozmieszczone:

```flux
from(bucket: "greenhouse")
  |> range(start: -12h)
  |> filter(fn: (r) => r._measurement == "tray" and r._field == "temperature")
  |> aggregateWindow(every: 10m, fn: mean)
  |> pivot(rowKey: ["_time"], columnKey: ["tray"], valueColumn: "_value")
```

## Alerty warte skonfigurowania

| Warunek | Znaczenie |
|---|---|
| brak rekordów `system` przez 5 min | sterownik offline lub zawieszony |
| `fault != "none"` | usterka wymagająca reakcji |
| `sensors_ok` < liczby tac | uszkodzony czujnik |
| `free_heap` < 8000 | wyciek pamięci — zgłoś jako błąd |
| `pulses_today` ≥ limitu | podlewanie nie nadąża albo czujnik jest źle wykalibrowany |

## Bufor offline

Przy awarii sieci rekordy trafiają do pierścienia 24 pozycji w RAM i są dosyłane
po odzyskaniu łączności — przerwa do ok. 3 minut nie zostawia luki w danych.
Dłuższe awarie powodują utratę najstarszych rekordów; jest to świadomy kompromis
wynikający z ilości pamięci ESP8266.
