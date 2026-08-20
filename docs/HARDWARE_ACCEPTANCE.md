# Hardware and Scanner Acceptance

Software validation confirms encoder structure and independent decode behavior. Release qualification still requires the exact Hosyond display module and target Zebra scanner because LCD optics, scanner engine, firmware, decoder settings, ambient light, viewing angle, protective films, and unit variation affect real scan performance.

## Equipment record

Record before testing:

- Hosyond module purchase/revision markings.
- ESP32 module marking and flash size.
- Display condition and whether a protective film is installed.
- Firmware Git commit or ZIP SHA-256 and reported firmware version.
- Zebra scanner model, serial number, scan engine, firmware, and configuration export.
- Host OS and USB-to-UART bridge/driver shown in Device Manager.
- Power source and cable.
- Ambient light source and approximate lux when available.

Use `docs/acceptance-results.csv` for observations.

## Device bring-up

1. Build `esp32dev` with PlatformIO and retain the complete build log.
2. Flash over the board's COM port.
3. Open a 115200-baud monitor and confirm the `ready` event reports protocol 1.0 and firmware 0.1.0.
4. Confirm portrait orientation, colors, backlight, and no visible tearing or corruption.
5. Touch all four corners and the center. Confirm hit targets correspond to their visual controls.
6. Type at least 40 characters across upper, lower, numeric, and symbol pages.
7. Insert `{FNC1}`, use backspace, clear, and display.
8. Save, load, list, and delete a preset. Power-cycle and confirm retained presets survive.
9. Turn backlight off/on through USB.
10. Generate a symbol without displaying, query status, display it, close it, redisplay it, download it, upload it, and compare metadata/CRC.
11. Interrupt an upload, send a wrong offset, wrong CRC, oversized matrix, overlong line, and malformed JSON. Confirm explicit errors and recovery without reboot.
12. Run for at least one hour while repeatedly changing symbols and polling status. Watch free heap and reset/fatal output.

## Scanner preparation

1. Factory-reset or capture the scanner's baseline configuration.
2. Enable only the decoder under test where practical. This reduces false classification.
3. Enable LCD/screen mode on scanner models that provide it.
4. Verify inverse decoding before testing inverted symbols.
5. Configure check-digit transmission and GS1 formatting consistently with expected values.
6. Capture whether the scanner emits AIM identifiers or transformed UPC/EAN values.

Not every Zebra scanning engine implements every symbology or every option. A failure must distinguish device generation, optical scanability, decoder availability, and scanner formatting.

## Baseline symbol set

Start with normal polarity, full backlight, default quiet zone, automatic rotation, and minimum module size 2.

| Symbology | Payload | Expected decoded data |
|---|---|---|
| QR | `LAB-TEST-001` | `LAB-TEST-001` |
| Data Matrix | `DM-ROUNDTRIP-123` | `DM-ROUNDTRIP-123` |
| Rectangular Data Matrix | `RECT-123` | `RECT-123` |
| Aztec | `AZTEC-ROUNDTRIP-123` | `AZTEC-ROUNDTRIP-123` |
| Aztec Rune | `42` | Scanner-specific representation, commonly zero-padded `042` |
| Code 128 | `Hello-123` | `Hello-123` |
| GS1-128 | `0109501101530003{FNC1}10ABC` | Scanner formatting-dependent; raw data contains GS before `10ABC` |
| Code 39 | `LAB-39` | `LAB-39` |
| UPC-A | `03600029145` | `036000291452` or scanner-configured EAN-13 expansion |
| EAN-13 | `590123412345` | `5901234123457` |
| EAN-8 | `5512345` | `55123457` |
| ITF | `12345670` | `12345670` |
| ITF-14 | `1001234500001` | `10012345000017` |
| Codabar | `A12345B` | Scanner configuration may include or strip start/stop characters |
| MSI Mod 10 | `12345` | `123455`, subject to configured checksum transmission |

## Scan matrix

For each required symbology, collect at least:

- Distances: near, nominal, and far within the scanner's working range.
- Angles: 0, 15, 30, and 45 degrees horizontally; repeat vertically for critical workflows.
- Ambient light: low, normal lab, and strong overhead/daylight when representative.
- Brightness: 100, 75, and 50 percent if the hardware/firmware later adds PWM control. Version 0.1.0 currently exposes on/off only, so external power/optical conditions may be used instead.
- Module size: minimum 1, 2, 3, and the largest fitting value for selected payloads.
- Quiet zone: default, minimum accepted by your scanner, and deliberately invalid/reduced negative test.
- Polarity: normal and inverted, after scanner configuration.
- Payload density: short, typical, and maximum practical payload that still meets the selected module-size policy.

A suggested baseline acceptance rule is 10 successful decodes in 10 trigger attempts at the nominal distance and 0-degree angle for every required production symbology, plus no incorrect decodes across negative/malformed fixtures. Adopt a stricter rule where the utility gates production or compliance decisions.

## Pixel and display inspection

Use a macro photo or microscope for representative QR, Data Matrix, and Code 128 screens:

- Module edges align to physical pixel rows/columns.
- Every module has a uniform integer width/height.
- No anti-aliased gray pixels occur at edges.
- Quiet zone is uniform and unobstructed.
- No UI text or touch overlay remains on the barcode screen.
- Black level and white level are uniform across the active area.
- Rotation does not introduce clipping.

## Power-loss and persistence

1. Save multiple presets, including maximum-length names and payloads.
2. Remove power while idle, while showing a barcode, and during a preset save.
3. Confirm filesystem mount behavior, surviving records, and whether format-on-failure occurred.
4. Repeat enough cycles to establish confidence for the lab's usage profile.
5. For production use, consider changing save to write a temporary file, flush/close, then rename atomically.

## Raw matrix qualification

- Upload a known PDF417 matrix generated by a trusted host library and scan it.
- Download the same matrix and compare dimensions, packed bytes, and CRC to the source.
- Test a non-byte-aligned width to confirm continuous packing.
- Upload a linear PBM with repeated rows using `--linear`; confirm host collapse to one row.
- Attempt a linear PBM whose rows differ; confirm the host rejects it.
- Exercise 512x512 storage bounds even though such a matrix cannot fit the physical display at one pixel per module.

## Release evidence

Attach to the release or lab validation record:

- PlatformIO build log and firmware hash.
- `docs/validation/independent-decoder-report.json`.
- Completed `acceptance-results.csv`.
- Scanner configuration export.
- Representative photos/video of scan tests.
- Serial protocol log for error-path and transfer tests.
- Deviations, waived formats, and scanner-specific transforms.
