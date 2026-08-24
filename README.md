# EspScreenBarcodeGenerator

[![Firmware CI](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/actions/workflows/firmware-ci.yml/badge.svg)](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/actions/workflows/firmware-ci.yml)
[![.NET CI](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/actions/workflows/dotnet-ci.yml/badge.svg)](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/actions/workflows/dotnet-ci.yml)
[![CodeQL](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/actions/workflows/codeql.yml/badge.svg)](https://github.com/JerrettDavis/EspScreenBarcodeGenerator/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A standalone and USB-orchestrated barcode laboratory utility for the Hosyond 3.5-inch 320x480 ESP32 display based on the ESP32-WROOM-32E, ST7796U TFT, and XPT2046 resistive touch controller.

The firmware generates barcode module patterns on the ESP32, renders them directly to the TFT with integer-sized pixels, stores reusable presets in LittleFS, and accepts newline-delimited JSON commands over the board's USB-to-UART connection. A full touch keyboard lets the device work without a computer.

Version: **0.1.0**  
Protocol: **1.0**

## Delivered capabilities

- Full on-device generation for QR Code, Data Matrix ECC200, Aztec and Aztec Rune, Code 128, GS1-128, Code 39, UPC-A, EAN-13, EAN-8, ITF, ITF-14, Codabar, and MSI Mod 10.
- Pixel-exact rendering with integer module sizes, configurable quiet zones, normal or inverted polarity, and automatic 0/90-degree orientation selection.
- Standalone touch UI with payload editor, four-page on-screen keyboard, FNC1 token insertion, symbology picker, options, and 32 persistent presets.
- USB serial commands to generate, display, close, return home, save, load, delete, list, upload, download, control the backlight, query status, and reboot.
- Chunked raw matrix transfer using continuous row-major, MSB-first packing and CRC32 validation.
- Python host utility for Windows, Linux, and macOS.
- Portable C++ tests, host utility tests, independent barcode decode validation, static hardware-contract checks, PlatformIO native tests, and CI ESP32 cross-build.

A display does not produce a separate category of "3D barcode." In scanner terminology, this utility covers one-dimensional linear symbols plus matrix and stacked two-dimensional symbols. A physically embossed, colored, holographic, or otherwise three-dimensional mark would require different hardware. PDF417 is not generated on-device in 0.1.0, but a PDF417 matrix produced by a host library can be uploaded and displayed through the raw matrix protocol.

## Hardware contract

This project intentionally preserves the known-good configuration from the existing `EspScreen` firmware.

| Function | GPIO / setting |
|---|---:|
| Display controller | ST7796U, 320x480, portrait |
| Touch controller | XPT2046 |
| SPI bus | HSPI |
| MISO | 12 |
| MOSI | 13 |
| SCLK | 14 |
| TFT CS | 15 |
| TFT DC | 2 |
| TFT reset | Not connected / `-1` |
| Backlight | 27, active high |
| Touch CS | 33 |
| Touch IRQ | 36, not required by TFT_eSPI polling |
| TFT frequency | 40 MHz |
| Touch frequency | 2.5 MHz |
| RGB order | BGR |
| Rotation | 0, portrait |
| Calibration | `{275, 3620, 264, 3532, 4}` |
| SD card SPI bus | VSPI (default pins, separate from the TFT/touch HSPI bus) |
| SD CS | 5 |
| SD MOSI | 23 |
| SD MISO | 19 |
| SD SCK | 18 |
| Battery ADC | 34, 2:1 resistor divider (no fuel-gauge IC) |

`USE_HSPI_PORT` is required. Removing it moves TFT_eSPI to the wrong SPI bus and can make touch/display behavior fail on this module.

The onboard microSD slot and battery-voltage sense pin are hardware-validated (SD detected/mounted and a plausible battery voltage read on both project boards); see [SdCardStore](include/SdCardStore.h) and [BatteryMonitor](include/BatteryMonitor.h). Store-presets-on-SD and show-battery-% are both on-screen Settings/Storage toggles, off and on by default respectively.

## Quick start on Windows

Install Python 3.11 or newer, then install PlatformIO Core:

```powershell
python -m pip install platformio==6.1.19
```

Build and flash, replacing `COM7` with the board's port:

```powershell
cd EspScreenBarcodeGenerator
.\scripts\flash.ps1 -Port COM7
```

Equivalent direct commands:

```powershell
python -m platformio run -e esp32dev
python -m platformio run -e esp32dev -t upload --upload-port COM7
python -m platformio device monitor --port COM7 --baud 115200
```

The first boot mounts LittleFS and creates `/presets`. The display starts with `LAB-TEST-001` selected as a QR payload.

## Standalone operation

1. Tap the symbology button to choose the barcode type.
2. Use the keyboard to edit the payload. `SYM` switches symbol/numeric pages. On the symbol page, the lower-left key inserts `{FNC1}`.
3. Open `OPTIONS` to adjust ECC, rotation, quiet zone, minimum module size, Data Matrix shape, inversion, checksum, and Aztec settings.
4. Tap `DISPLAY` or `GO`.
5. Scan the full-screen symbol. Tap the screen after the short close guard to return home.
6. Save frequently used specifications in one of the 32 automatic `SLOT01` through `SLOT32` preset records.

The UI stores specifications, not rendered bitmaps. Loading a preset regenerates the symbol on-device.

## USB host utility

Install the client dependency:

```powershell
python -m pip install -r tools/requirements.txt
```

Basic commands:

```powershell
python tools/espbarcode.py --port COM7 hello
python tools/espbarcode.py --port COM7 capabilities
python tools/espbarcode.py --port COM7 status
python tools/espbarcode.py --port COM7 generate qr "LAB-TEST-001"
python tools/espbarcode.py --port COM7 generate datamatrix "DM-ROUNDTRIP-123"
python tools/espbarcode.py --port COM7 generate gs1-128 "0109501101530003{FNC1}10ABC"
python tools/espbarcode.py --port COM7 close
```

Persist and recall a symbol:

```powershell
python tools/espbarcode.py --port COM7 generate code128 "LOT-2026-00042" --save-as LOT_SAMPLE
python tools/espbarcode.py --port COM7 list
python tools/espbarcode.py --port COM7 load LOT_SAMPLE --display
python tools/espbarcode.py --port COM7 delete LOT_SAMPLE
```

Transfer the current module matrix:

```powershell
python tools/espbarcode.py --port COM7 download current.json --pbm current.pbm
python tools/espbarcode.py --port COM7 upload current.json
python tools/espbarcode.py --port COM7 upload externally-generated.pbm --quiet 4 --rotation auto
```

Prefix a payload argument with `@` to read UTF-8 text from a file:

```powershell
python tools/espbarcode.py --port COM7 generate qr @payload.txt
```

Arbitrary binary payloads can be sent directly through protocol field `data_base64`; the convenience CLI currently treats `generate` input as UTF-8 text.

See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the complete wire contract.

## .NET client

A .NET 10 / C# 14 client library, CLI demo, and xUnit test suite live in [`dotnet/`](dotnet/README.md):

```powershell
cd dotnet
dotnet test EspScreenBarcodeGenerator.slnx
dotnet run --project src/EspBarcode.Cli -- demo --port COM7
```

The CLI's `demo` command exercises QR, UPC-A, and Code 128 generation on-device, plus a
`license` scenario that renders a PDF417 driver's-license-style symbol on the host with
ZXing.Net (the firmware can't generate PDF417 on-board) and uploads it through the
raw-matrix protocol. See [`dotnet/README.md`](dotnet/README.md) for details.

A standalone generator/viewer (`EspBarcode.Viewer.Cli` +
`EspBarcode.Viewer.Gui`) replicates the ESP's barcode generation and
display behavior without any device connected — see
[`dotnet/README.md`](dotnet/README.md#standalone-generatorviewer-no-esp-required).

## Symbology behavior

| Type | Device name | Important behavior |
|---|---|---|
| QR Code | `qr` | Model 2, versions 1-40, ECC L/M/Q/H. UI defaults to max version 20; USB can select up to 40. |
| Data Matrix | `datamatrix` | ECC200 square or rectangular. ASCII encodation with digit-pair compaction and upper shift for bytes above 127. |
| Aztec | `aztec` | Full/compact auto-selection, configurable security and minimum layers. `aztec_layers=0` plus numeric 0-255 creates an Aztec Rune. |
| Code 128 | `code128` | Automatic Code Set B/C switching. `{FNC1}`, `<FNC1>`, `<GS>`, and ASCII GS are recognized as FNC1/group separator input. |
| GS1-128 | `gs1-128` | Adds the leading FNC1 and supports embedded separators. The caller remains responsible for valid GS1 application-identifier data. |
| Code 39 | `code39` | Converts letters to uppercase. Standard character set only; start/stop added automatically. |
| UPC-A | `upca` | Accepts 11 digits and calculates the check digit, or validates 12 digits. |
| EAN-13 | `ean13` | Accepts 12 digits and calculates the check digit, or validates 13 digits. |
| EAN-8 | `ean8` | Accepts 7 digits and calculates the check digit, or validates 8 digits. |
| ITF | `itf` | Digits only. Odd-length input is left-padded with `0`. |
| ITF-14 | `itf14` | Accepts 13 digits and calculates the check digit, or validates 14 digits. |
| Codabar | `codabar` | Adds `A` start/stop characters when omitted. |
| MSI | `msi` | Digits only; optional single Mod 10 check digit. |

### Current encoder limits

- Device payload limit: 2048 bytes. Individual symbologies often have lower capacity.
- Device raw matrix limit: 512x512 modules and 32,768 packed bytes.
- Data Matrix does not yet emit ECI, Macro, structured append, or GS1 Data Matrix FNC1 codewords.
- QR structured append, Micro QR, GS1 QR, and ECI are not exposed.
- Code 128 supports the lab-oriented FNC1 path but not an arbitrary FNC2/FNC3/FNC4 API.
- PDF417, MicroPDF417, MaxiCode, GS1 DataBar, postal codes, and composite symbols require raw matrix upload in 0.1.0.
- Human-readable text is intentionally omitted from the scan surface to maximize module size and contrast.

## Scanner setup

For a Zebra handheld scanner:

- Enable the decoder for the symbology under test. Decoder availability varies by scanner engine and configuration.
- Enable the scanner's LCD or screen mode when the model provides it.
- Start with normal black modules on a white background, full display backlight, and the default quiet zone.
- Disable automatic brightness and screen dimming during a test run.
- Keep the display perpendicular to the scanner, then introduce angle/distance variation as an explicit test dimension.
- Configure inverse decoding before using `invert=true`.
- Record scanner model, engine, firmware, decoder configuration, distance, angle, ambient light, and pass rate in `docs/acceptance-results.csv`.

## Validation

Portable validation:

```bash
python3 -m pip install -r tools/validation-requirements.txt
./scripts/run_validation.sh
```

PlatformIO native tests and ESP32 cross-build:

```bash
python3 -m pip install platformio==6.1.19
./scripts/run_platformio_validation.sh
```

Windows equivalents:

```powershell
python -m pip install -r tools/validation-requirements.txt
.\scripts\run_validation.ps1

python -m pip install platformio==6.1.19
.\scripts\run_platformio_validation.ps1
```

The portable suite performs:

- Warning-as-error C++17 compilation.
- Core unit tests for packing, Base64, parsers, check digits, encoders, limits, and layout.
- Python tests for matrix JSON/PBM conversion, CRCs, row padding, and transfer validation.
- Static checks for the exact TFT/touch pin and calibration contract plus the required USB command surface.
- Independent decode round trips through MuPDF for 13 generated barcode cases.

The PlatformIO suite additionally resolves the real QR dependency, runs the QR adapter test, compiles the portable library under PlatformIO, and cross-builds the complete ESP32 firmware.

See [docs/VALIDATION.md](docs/VALIDATION.md) and [docs/HARDWARE_ACCEPTANCE.md](docs/HARDWARE_ACCEPTANCE.md). Software tests do not replace a physical scan qualification against the exact Zebra scanner and display unit.

## Gateway mode and pairing

One board can relay EspLink v2 traffic between a USB-connected host and a second, ESP-NOW-connected display, and boards enforce mutual trust (ECDSA/ECDH pairing with a numeric confirmation code) before relaying to each other. Both can be driven entirely on-screen — from the device's own touchscreen or from the Blazor controller in a browser. See [docs/GATEWAY_MODE_AND_PAIRING.md](docs/GATEWAY_MODE_AND_PAIRING.md) for the step-by-step guide with screenshots.

## Project layout

```text
include/                       Firmware application headers
src/                           TFT UI, preset storage, serial protocol, entry point
lib/EspBarcodeCore/            Portable barcode encoders and layout engine
tools/espbarcode.py            USB serial client and matrix conversion utility
tools/native_cli.cpp           Native symbol renderer used by validation
test/test_native/              PlatformIO Unity tests
tests/                         Portable and host validation suites
dotnet/                        .NET 10/C# 14 client library, CLI demo, and xUnit tests
docs/                          Architecture, protocol, validation, acceptance plan
scripts/                       Validation and Windows flashing helpers
.github/workflows/             Firmware CI, .NET CI, CodeQL, Dependabot automerge
```

## Design principles

- A barcode is rendered only with whole TFT pixels per module. No interpolation, anti-aliasing, sprite scaling, or fractional coordinates are used.
- A symbol that cannot fit at the requested minimum module size fails explicitly instead of displaying a likely unscannable image.
- The portable core has no Arduino dependency. Hardware, storage, UI, and transport remain adapters around it.
- USB commands are transport-neutral JSON messages even though the proof of concept uses USB-to-UART. A future Windows driver can retain the command model.
- Raw matrix upload provides an escape hatch without forcing every barcode encoder into ESP32 firmware.

## License and attribution

Project code is MIT licensed. Data Matrix and Aztec work include adaptations from Alois Zingl's MIT-licensed 2D-Barcode reference implementation. QR generation uses Richard Moore's MIT-licensed `QRCode` library, which includes work derived from Project Nayuki. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
