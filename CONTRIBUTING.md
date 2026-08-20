# Contributing

Thanks for considering a contribution to EspScreenBarcodeGenerator.

## Project layout

- `src/`, `lib/`, `include/` — ESP32 firmware (PlatformIO, Arduino framework).
- `docs/PROTOCOL.md` — the USB serial protocol contract. Any change to request/response
  shape is a protocol change; bump the protocol version and update this doc in the same PR.
- `tools/espbarcode.py` — Python host utility and reference client implementation.
- `tests/` — portable C++ tests (CMake/ctest), Python host-tool tests, static
  hardware-contract checks, and independent decoder validation.
- `test/test_native/` — PlatformIO's own native Unity test target.
- `dotnet/` — .NET 10 client library, CLI demo, and xUnit tests.

## Building and testing

### Firmware

```powershell
python -m pip install platformio==6.1.19
python -m platformio test -e native      # portable Unity tests
python -m platformio run -e esp32dev     # cross-compile the firmware
```

### Full native validation suite (CMake + ctest + Python + decoder validation)

```powershell
python -m pip install cmake ninja
python -m pip install -r tools/requirements.txt -r tools/validation-requirements.txt
./scripts/run_validation.ps1
```

### .NET client

```powershell
cd dotnet
dotnet test EspScreenBarcodeGenerator.slnx
```

## Making a change

1. If you touch the wire protocol, update `docs/PROTOCOL.md` first — treat it as the spec.
2. Add or update tests alongside the change (native C++ tests for encoder/layout logic,
   Python tests for the host tool, xUnit tests for the .NET client).
3. Run the relevant test suite(s) above before opening a PR.
4. If you can, verify on real hardware and note the board/port in the PR description —
   CI only builds the firmware, it can't flash or exercise a real device.

## Reporting bugs / requesting features

Use the issue templates — they ask for the details (firmware version, protocol traffic,
reproduction steps) that make a report actionable.

## Code of conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).
