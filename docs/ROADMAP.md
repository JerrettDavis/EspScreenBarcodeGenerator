# Roadmap

## v0.1.0 - Delivered proof of concept

- Preserve the known-good ST7796U/XPT2046 hardware configuration.
- Standalone touch UI and keyboard.
- On-device QR, Data Matrix, Aztec, Code 128/GS1-128, Code 39, UPC/EAN, ITF, Codabar, and MSI generation.
- Persistent specification presets.
- Versioned NDJSON USB serial protocol.
- Raw packed matrix upload/download with CRC32.
- Python orchestration utility.
- Portable tests, independent decode validation, CI native tests, and ESP32 cross-build.

## v0.2 - Barcode breadth and qualification

- Implement PDF417 and compact PDF417 on-device.
- Add GS1 Data Matrix FNC1 semantics and application-identifier helpers.
- Add GS1 QR and explicit ECI/binary payload workflows.
- Evaluate Micro QR and MicroPDF417 against practical TFT module sizes.
- Add Code 93, Code 11, UPC-E, and GS1 DataBar where scanner requirements justify them.
- Add scanner-profile presets for minimum module size, quiet zone, polarity, and orientation.
- Complete physical qualification against named Zebra scanner models and store results as release evidence.

## v0.3 - Lab automation

- Named preset entry on the touch UI.
- Ordered test playlists with next/previous, timed advance, pause, and repeat.
- Host-created campaigns containing payload variations, expected decoded data, and metadata.
- Device event stream for screen changes and touch acknowledgement.
- Optional scanner feedback input to associate scan result and latency with the displayed fixture.
- Exportable run logs with symbol hash, timing, scanner response, and pass/fail.

## v0.4 - Transport and fleet management

- Wi-Fi HTTP/WebSocket transport feeding the same command model.
- BLE transport where bandwidth permits.
- Device identity, configuration profiles, signed firmware version reporting, and fleet discovery.
- OTA update with rollback and release-channel policy.
- Authentication and authorization for network transports.

## Hardware evolution

The ESP32-WROOM-32E board is appropriate for the serial PoC. True native USB CDC, HID, or vendor-class behavior requires a USB-capable MCU such as ESP32-S2/S3 or an external USB device controller. A future board should retain the same logical protocol and barcode core while replacing the transport adapter.

## Backlog quality gates

Each new symbology should include:

1. Spec and capacity analysis for a 320x480 LCD.
2. Portable unit vectors and invalid-input cases.
3. Independent decoder round trips where a separate decoder supports it.
4. PlatformIO native and ESP32 cross-build.
5. Physical Zebra scanner matrix across nominal distance/angle/light.
6. Capability/protocol/host/UI documentation updates.
7. Third-party license review when adapting an implementation.
