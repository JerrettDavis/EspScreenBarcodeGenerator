# Validation Record

Validation date: **2026-08-19**  
Project version: **0.1.0**

## Completed in the delivery environment

### Portable C++ build

The portable barcode core and native utilities compiled as C++17 with:

- GCC 14.2.0
- Clang 17.0.0
- `-Wall -Wextra -Wpedantic -Werror`
- No Arduino headers in the core

Clean GCC and Clang builds passed. Both compiler paths were also exercised with AddressSanitizer and UndefinedBehaviorSanitizer enabled; no memory-safety or undefined-behavior finding was reported by the native core tests.

### Native core tests

`tests/native_core_tests.cpp` passed. Covered areas include:

- Packed matrix dimensions, bounds, bit ordering, mutation, and clearing.
- Base64 encode/decode and malformed input rejection.
- Symbology, rotation, and ECC parsing.
- Code 128 and GS1 separator normalization.
- Code 39 normalization.
- EAN-13, EAN-8, UPC-A, and ITF-14 check-digit calculate/validate paths.
- ITF odd-length normalization.
- Codabar start/stop normalization.
- MSI Mod 10 calculation.
- Data Matrix square and rectangular generation.
- Aztec and Aztec Rune generation.
- Integer pixel layout and dense-symbol rejection.
- 2,048-byte payload bound.

The plain native build does not have the PlatformIO-managed `ricmoo/QRCode` header, so its QR-specific assertion is conditionally omitted. `test/test_native/test_main.cpp` contains an unconditional QR adapter test for the PlatformIO `native` environment.

### Independent decode validation

The native encoder rendered PBM images that were decoded by a separate implementation, MuPDF through PyMuPDF 1.26.7. All 13 cases passed:

| Case | Result |
|---|---|
| Code 128 | Pass |
| GS1-128 with embedded FNC1/GS | Pass |
| Code 39 | Pass |
| EAN-13 | Pass |
| EAN-8 | Pass |
| UPC-A | Pass |
| ITF | Pass |
| ITF-14 | Pass |
| Codabar | Pass |
| Data Matrix ECC200 square | Pass |
| Data Matrix ECC200 rectangular | Pass |
| Aztec | Pass |
| Aztec Rune | Pass |

The machine-readable report is `docs/validation/independent-decoder-report.json`.

MSI is covered by structural/checksum unit tests but was not included in this independent round trip because the available independent decoder path did not expose MSI. QR is exercised by the real PlatformIO dependency test but was not part of the delivery environment's plain CMake independent-decode run.

### Host utility tests

Seventeen Python unit tests passed, covering:

- Matrix JSON round trip.
- CRC tamper rejection.
- PBM P1 continuous MSB-first packing.
- PBM P4 row-padding conversion.
- Linear PBM row collapse.
- Rejection of inconsistent linear rows.
- PBM writing and re-reading.
- Dimension, rotation, quiet-zone, and CRC validation.
- NDJSON request ID correlation and structured device errors.
- Complete, missing, overlapping, malformed, and out-of-range download chunks.
- Upload byte-count, offset, accepted-length, and CRC acknowledgements.

### Static firmware contract

`tests/static_firmware_checks.py` passed and guards:

- Pinned ESP32 platform.
- ST7796U dimensions and driver flag.
- Exact HSPI pins, backlight pin, touch chip select, BGR order, frequencies, and `USE_HSPI_PORT`.
- Exact touch calibration and portrait rotation.
- Direct integer module rendering path.
- Required USB command set and safety/error tokens.
- Absence of the unused LVGL dependency.

## PlatformIO and ESP32 build status

The project includes two reproducible checks:

```bash
pio test -e native
pio run -e esp32dev
```

CI runs both with PlatformIO Core 6.1.19, Espressif32 platform 7.0.1, TFT_eSPI 2.5.43, ArduinoJson 7.4.3, and ricmoo/QRCode 0.0.1.

The delivery sandbox did not contain PlatformIO or an Xtensa ESP32 compiler, and outbound package installation was blocked by DNS. An actual local ESP32 cross-build therefore could not be executed in that sandbox. This is recorded rather than represented as a completed check. The checked-in CI workflow performs the missing dependency-resolved native QR test and full firmware cross-build on a normal connected runner.

## Physical hardware status

No Hosyond display or Zebra scanner was connected to the delivery environment. The following remain physical acceptance activities:

- Flashing the exact board.
- Visual confirmation of rotation, color order, touch calibration, and backlight.
- Zebra decoding across scanner model, LCD mode, distance, angle, brightness, ambient light, and decoder configuration.
- Long-run heap/stability observation.
- Power-loss and LittleFS persistence testing.

The exact procedure and evidence template are in `docs/HARDWARE_ACCEPTANCE.md` and `docs/acceptance-results.csv`.

## Reproduce portable validation

```bash
python3 -m pip install -r tools/validation-requirements.txt
./scripts/run_validation.sh
```

Optional sanitizers:

```bash
ESPBARCODE_ENABLE_SANITIZERS=ON \
ESPBARCODE_BUILD_DIR=.build/native-sanitized \
./scripts/run_validation.sh
```

PowerShell:

```powershell
.\scripts\run_validation.ps1
.\scripts\run_validation.ps1 -EnableSanitizers -BuildDirectory .build/native-sanitized
```

## Reproduce PlatformIO validation

```bash
python3 -m pip install platformio==6.1.19
./scripts/run_platformio_validation.sh
```

PowerShell:

```powershell
python -m pip install platformio==6.1.19
.\scripts\run_platformio_validation.ps1
```

## Acceptance boundary

A passing software report means the encoded module patterns are structurally valid for the tested vectors and the project is configured for a reproducible ESP32 build. It does not prove optical reliability on every scanner. The release should be considered lab-qualified only after the required Zebra models meet the agreed scan-rate criteria and the results are retained with the firmware hash.
