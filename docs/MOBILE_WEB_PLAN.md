# Mobile wireless control plan

## Decision

Extend the existing Blazor WebAssembly controller as an installable PWA. Android Chromium can use the
firmware's existing BLE GATT service through Web Bluetooth; desktop Chromium retains Web Serial. Raw TCP
and Wi-Fi Direct are not browser APIs, and iOS does not expose Web Bluetooth, so Wi-Fi browser support
requires a firmware HTTPS/WebSocket carrier (or a small native wrapper) in a later phase.

## Delivery phases

1. Installable, responsive PWA; direct BLE discovery, connection, EspLink v2 hello/generate, multi-target send.
2. Camera/gallery barcode import using `BarcodeDetector`, with decoded data/type prefilled for review.
3. Browser-compatible Wi-Fi endpoint: ESP SoftAP/station discovery plus authenticated WSS EspLink framing;
   define radio arbitration with ESP-NOW before enabling it alongside gateway mode.
4. Unify serial, BLE, Wi-Fi, and relayed peers in one multi-select workflow. Direct serial clients,
   gateway relays, and individually paired gateway peers are available in the mobile picker; the host
   stamps each paired peer's persisted `route_id` into EspLink hop frames for unambiguous encrypted routing.
5. Validate Android hardware, two-screen gateway operation, offline installation, reconnect behavior, and
   threat model. Keep deterministic fake-transport unit/E2E coverage for every workflow.

## Security and compatibility gates

- Web Bluetooth requires HTTPS (localhost is allowed) and a user gesture for the chooser.
- Current firmware accepts any BLE central; production use requires authenticated pairing/trust.
- The current 200-byte BLE hop-frame profile depends on a negotiated ATT MTU and needs explicit failure UX.
- Wi-Fi must not be enabled until its channel/radio coexistence with ESP-NOW is deterministic.
