# Changelog

## Unreleased

- Fixed `LittleFS.begin(true)` using the Arduino-ESP32 default partition label
  `"spiffs"` instead of the `littlefs` label declared in `partitions.csv`, which
  silently prevented the filesystem (and preset storage) from mounting on real
  hardware.
- Fixed the native (`env:native`) PlatformIO/CMake build silently dropping the
  `ricmoo/QRCode` include path due to LDF/CMake framework-compatibility
  filtering, which disabled QR encoding in that build target.
- Fixed `test/test_native/test_main.cpp` missing Unity's required `setUp`/
  `tearDown` definitions, which broke the native test link.
- Fixed GS1-128 `generate` responses embedding the raw ASCII 0x1D group
  separator directly in `normalized_data`; ArduinoJson does not escape
  arbitrary control bytes, so the response was invalid JSON for strict
  parsers. Now reported as `<GS>`.
- Fixed `tools/espbarcode.py`'s host client raising on the first non-JSON
  line instead of skipping it, which broke any request issued immediately
  after a DTR-triggered board reset (boot-log chatter shares the UART).
- Added a .NET 10 / C# 14 client (`dotnet/`): `EspBarcode.Client` (protocol
  client library), `EspBarcode.Cli` (demo console app covering QR, UPC-A,
  Code 128, and a PDF417 "driver's license" scenario via ZXing.Net + the
  raw-matrix upload protocol), and an xUnit test suite exercising the
  protocol client against a scripted fake transport.
- Added GitHub scaffolding: firmware CI, .NET CI, CodeQL, Dependabot (with
  auto-merge for patch updates), issue/PR templates, `CONTRIBUTING.md`,
  `CODE_OF_CONDUCT.md`, and `SECURITY.md`.

## 0.1.0 - 2026-08-19

- Created new PlatformIO firmware for the Hosyond ESP32 ST7796U/XPT2046 display.
- Added direct touch UI, multi-page keyboard, FNC1 insertion, options, full-screen scan view, and persistent presets.
- Added on-device QR, Data Matrix ECC200, Aztec/Rune, Code 128/GS1-128, Code 39, UPC-A, EAN-13, EAN-8, ITF/ITF-14, Codabar, and MSI generation.
- Added integer-only module layout with explicit fit rejection.
- Added USB-to-UART NDJSON protocol 1.0 with command IDs, structured errors, generation/control/preset commands, and raw matrix upload/download.
- Added Python orchestration and PBM/JSON matrix conversion utility.
- Added portable unit tests, PlatformIO Unity tests, host tests, static hardware-contract checks, independent decoder validation, CMake build, CI, documentation, and hardware acceptance procedure.
- Deferred on-device PDF417 to the next encoder milestone; raw upload supports externally generated PDF417 matrices now.
