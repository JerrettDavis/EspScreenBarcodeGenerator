#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ESPBARCODE_BUILD_DIR:-$ROOT/.build/native-validation}"
GENERATOR=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR=(-G Ninja)
fi

python3 - <<'PY'
try:
    import pymupdf
except ImportError as exc:
    raise SystemExit("PyMuPDF is required. Run: python -m pip install -r tools/validation-requirements.txt") from exc
PY

cmake -S "$ROOT" -B "$BUILD_DIR" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DESPBARCODE_ENABLE_SANITIZERS="${ESPBARCODE_ENABLE_SANITIZERS:-OFF}"
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
python3 -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
python3 "$ROOT/tests/static_firmware_checks.py"
python3 "$ROOT/tests/validate_symbols.py" \
  --cli "$BUILD_DIR/barcode_native" \
  --report "$ROOT/docs/validation/independent-decoder-report.json"

echo "All portable validation suites passed."
