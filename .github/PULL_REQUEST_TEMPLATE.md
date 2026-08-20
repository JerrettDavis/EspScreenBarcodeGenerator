## Summary

<!-- What changed and why. -->

## Area(s) touched

- [ ] Firmware (`src/`, `lib/`, `include/`)
- [ ] USB protocol / `docs/PROTOCOL.md`
- [ ] Python host tool / tests (`tools/`, `tests/`)
- [ ] .NET client / CLI / tests (`dotnet/`)
- [ ] CI / build tooling

## Testing

<!-- What you ran, and against real hardware or native/CI validation only. -->

- [ ] `pio test -e native`
- [ ] `pio run -e esp32dev`
- [ ] `python -m unittest discover -s tests -p "test_*.py"`
- [ ] `tests/static_firmware_checks.py`
- [ ] `dotnet test dotnet/EspScreenBarcodeGenerator.slnx`
- [ ] Verified on real hardware (describe board/port below)

## Notes for reviewers

<!-- Anything a reviewer should know: protocol version implications, breaking changes, follow-ups. -->
