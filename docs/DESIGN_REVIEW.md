# Design Review and Decisions

## Review of the source firmware

The existing `EspScreen` firmware was treated as a hardware bring-up reference, not as an application base. The reusable facts are the board target, TFT controller, display orientation, HSPI pin mapping, touch chip-select and calibration, backlight pin, BGR ordering, and safe SPI frequencies.

The new firmware intentionally does not inherit the unrelated application UI or LVGL dependency. A barcode scan surface benefits from a smaller rendering stack: direct TFT primitives preserve exact pixels, use less RAM, and make scan failures easier to diagnose.

## Key decisions

### Generate on-device

The normal path sends a symbology plus payload/options. The ESP32 creates the module matrix and displays it. This makes the device reproducible and keeps host orchestration lightweight.

### Keep raw matrix upload

Not every lab fixture should wait for an embedded encoder. Raw upload supports PDF417 in the initial release, proprietary test symbols, intentionally malformed matrices, golden fixtures, and future formats while preserving the same display and download controls.

### Use USB serial for the proof of concept

The ESP32-WROOM board is connected through USB-to-UART, so a custom kernel driver would add deployment cost without increasing device capability. NDJSON over the COM port is observable, scriptable, and sufficient for the PoC. The protocol is structured so a future Windows service or driver can wrap it without changing higher-level commands.

### Do not rescale images

The renderer only paints integer module rectangles. It refuses a symbol that cannot meet the requested minimum module size. This is safer than accepting a command and showing an attractive but unreliable barcode.

### Direct touch UI instead of LVGL

The firmware has five simple views and a fixed 320x480 screen. Direct drawing avoids a large UI dependency and accidental filtering or focus behavior on the barcode screen. The barcode itself occupies the complete display and closes on a guarded touch.

### Persist specifications, not images

Preset files are small, understandable, and regenerate using current firmware. Raw matrices are transferable but not persisted in v0.1.0, preventing a large or stale bitmap catalog from consuming flash.

## Threat and failure review

| Risk | Current control | Remaining work |
|---|---|---|
| Invalid or malicious serial input | 4 KiB line bound, JSON object validation, typed fields, dimension limits, Base64 validation | Fuzz ArduinoJson command parsing on target or an exact host shim |
| Partial/corrupt transfer | Sequential offsets, declared byte count, CRC32 | Add resumable/retry protocol only if lab networks require it |
| Scanner cannot read LCD | Pixel-exact rendering, quiet zones, contrast, min module bound | Qualify exact Zebra scanner engine, LCD mode, angle, distance, brightness |
| Flash corruption/power loss | LittleFS and bounded JSON records | Power-cut tests; consider atomic temp-file/rename save path |
| Wrong board revision/pins | Static contract test and documented pin map | Add runtime board-ID/config profile if multiple Hosyond revisions emerge |
| Dense symbol technically generated but practically poor | Explicit min-module option, fit rejection | Build per-scanner policy profiles with stronger minimums |
| Inverted symbol not enabled on scanner | Explicit opt-in | Add scanner profile/preset metadata |
| Unsupported barcode requested | Capability query and clear error | Add PDF417, GS1 Data Matrix, Micro QR, DataBar as prioritized |
| Serial command changes break automation | Protocol version and IDs | Add JSON schema files and compatibility tests in a later release |

## Definition of done for v0.1.0

- PlatformIO project contains exact board/display/touch configuration.
- Device can generate every advertised symbology without host-rendered images.
- Touch UI can enter text, choose type/options, display, close, and use presets.
- USB protocol covers control, persistence, generation, upload, download, display, and lifecycle actions.
- Host tool automates the protocol and converts PBM/JSON matrices.
- Portable encoders compile warning-free and pass unit tests.
- Representative symbols round-trip through an independent decoder.
- PlatformIO CI runs the actual QR dependency test and ESP32 cross-build.
- Physical qualification procedure is documented without representing unperformed hardware tests as complete.
