#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if command -v pio >/dev/null 2>&1; then
  PIO=(pio)
elif python3 -m platformio --version >/dev/null 2>&1; then
  PIO=(python3 -m platformio)
else
  echo "PlatformIO Core is not installed. Run: python3 -m pip install platformio==6.1.19" >&2
  exit 2
fi

"${PIO[@]}" test -e native
"${PIO[@]}" run -e esp32dev
