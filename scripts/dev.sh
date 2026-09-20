#!/usr/bin/env bash
# Skrót do najczęstszych zadań deweloperskich.
# Użycie: ./scripts/dev.sh <komenda>
set -euo pipefail

cd "$(dirname "$0")/.."

usage() {
    cat <<'EOF'
Dostępne komendy:

  test        testy jednostkowe logiki (bez sprzętu)
  build       kompilacja firmware dla obu wariantów
  format      clang-format na wszystkich źródłach
  lint        clang-format --dry-run + cppcheck (tak jak w CI)
  flash       wgranie firmware przez USB
  flashfs     wgranie panelu WWW do LittleFS
  monitor     podgląd portu szeregowego
  ci          pełny zestaw kontroli uruchamiany przez CI
EOF
}

sources() {
    find src lib test -name '*.cpp' -o -name '*.h'
}

case "${1:-}" in
    test)    pio test -e native ;;
    build)   pio run -e nodemcuv2 && pio run -e nodemcuv2-nomux ;;
    format)  sources | xargs clang-format -i && echo "Sformatowano." ;;
    lint)
        sources | xargs clang-format --dry-run --Werror
        pio check -e nodemcuv2 --fail-on-defect=medium --fail-on-defect=high
        ;;
    flash)   pio run -e nodemcuv2 -t upload ;;
    flashfs) pio run -e nodemcuv2 -t uploadfs ;;
    monitor) pio device monitor ;;
    ci)
        "$0" lint
        "$0" test
        "$0" build
        echo "Wszystkie kontrole przeszły."
        ;;
    *) usage; exit 1 ;;
esac
