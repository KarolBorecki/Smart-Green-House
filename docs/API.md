# REST API

Adres bazowy: `http://herb-greenhouse.local` (lub adres IP z logów).
Wszystkie odpowiedzi w formacie JSON.

> API nie ma uwierzytelniania — sterownik powinien pracować w sieci domowej,
> bez przekierowania portów z internetu.

## `GET /api/status`

```json
{
  "version": "1.0.0",
  "device": "herb-01",
  "uptime_s": 84213,
  "fault": "none",
  "safe_mode": false,
  "readings": {
    "temperature": 21.7,
    "temperature_min": 21.1,
    "temperature_max": 22.4,
    "humidity": 58.2,
    "sensors_ok": 3,
    "dry_trays": 1,
    "target_temperature": 22.0
  },
  "actuators": {
    "pump": false, "fan": true, "heater": false, "light": true,
    "fan_reason": "circulation"
  },
  "irrigation": { "state": "idle", "pulses_today": 3, "fault": "none" },
  "overrides": []
}
```

`fault`: `none`, `sensor_timeout`, `over_temperature`, `reservoir_empty`,
`irrigation_lockout`.
`fan_reason`: `off`, `overtemp`, `humidity`, `circulation`, `post_heat`.
`irrigation.state`: `idle`, `pulsing`, `soaking`, `fault`.

## `GET /api/config` · `POST /api/config`

Odczyt i zmiana nastaw uprawy. Pola pominięte zachowują dotychczasową wartość,
a zmiany są zapisywane w LittleFS i przeżywają restart.

```bash
curl -X POST http://herb-greenhouse.local/api/config \
  -H 'Content-Type: application/json' \
  -d '{"climate":{"target":23.0,"night":19.0},"light":{"on_minute":360,"off_minute":1320}}'
```

`on_minute` i `off_minute` to minuty od północy czasu lokalnego (360 = 06:00).

Nastawy przechodzą walidację; przy błędzie zwracany jest kod **422** i przyczyna:

| Kod | Znaczenie |
|---|---|
| `target_out_of_range` | temperatura docelowa poza 5–35 °C |
| `night_above_day` | nastawa nocna wyższa od dziennej |
| `max_temp_below_target` | próg wentylacji nie jest wyższy od nastawy |
| `critical_temp_below_max` | limit krytyczny nie jest wyższy od progu wentylacji |
| `hysteresis_out_of_range` | histereza poza 0,1–5 °C |
| `humidity_out_of_range` | próg wilgotności poza 30–95 % |
| `light_minute_out_of_range` | minuta doby poza 0–1439 |
| `pulse_out_of_range` | impuls poza 1–120 s |
| `soak_shorter_than_pulse` | przerwa krótsza niż impuls |
| `max_pulses_out_of_range` | limit dobowy poza 1–60 |

## `POST /api/override`

Ręczne przejęcie kontroli nad urządzeniem. `actuator`: `pump`, `fan`, `heater`,
`light`.

```bash
# Pompa na 60 s
curl -X POST .../api/override -H 'Content-Type: application/json' \
  -d '{"actuator":"pump","value":true,"ttl_ms":60000}'

# Wymuszone wyłączenie lamp na 2 h
curl -X POST .../api/override -H 'Content-Type: application/json' \
  -d '{"actuator":"light","value":false,"ttl_ms":7200000}'

# Powrót pod kontrolę automatyki
curl -X POST .../api/override -H 'Content-Type: application/json' \
  -d '{"actuator":"light","clear":true}'
```

`ttl_ms` jest ograniczone do 6 h; wartość pominięta lub zerowa daje 15 minut.
Nadpisanie nie może przełamać blokady bezpieczeństwa — przy przegrzaniu grzanie
i tak pozostanie wyłączone.

## `POST /api/faults/reset`

Kasuje blokadę nawadniania (`reservoir_empty`, `irrigation_lockout`).
Wywołuj po uzupełnieniu wody w zbiorniku.

## `GET /health`

Zwraca `200 ok` — sonda dla monitoringu (np. Uptime Kuma).
