# Mobile controller hardware validation — 2026-08-25

This record captures the real-device checks performed for the mobile PWA/Bluetooth work on a
Windows host. It supplements the scanner-focused matrix in `docs/HARDWARE_ACCEPTANCE.md`; it does
not claim optical Zebra-scanner qualification or unattended longevity testing.

## Devices and firmware

| Role | Port | USB bridge | ESP32 MAC | Result |
|---|---|---|---|---|
| Screen A / gateway | COM7 | CH340 (`1A86:7523`) | `B4:BF:E9:0F:B1:34` | Flashed and verified |
| Screen B / client | COM15 | CH340 (`1A86:7523`) | `B0:CB:D8:03:AE:50` | Flashed and verified |

- PlatformIO environment: `esp32dev`, Espressif32 `7.0.1`, Arduino framework.
- Reported firmware: `0.1.0`, protocol `1.0`, display `320x480`.
- Firmware binary SHA-256: `BD1FF27BE995E0480FBC40B608F2203ABCD060FCDA88961BD2F9C92112AA2B76`.
- Flash verification: esptool verified every written segment on both boards and hard-reset each board.
- Build size: 1,920,433 / 3,145,728 flash bytes (61.0%); 70,584 / 327,680 RAM bytes (21.5%).

## Direct serial checks

- Both boards answered `hello` and `status` after flashing.
- COM7 generated and displayed QR payload `MOBILE-HW-COM7-20260825` at ECC H, returning a
  `21x21` matrix. Download returned 56 bytes with CRC32 `596549596`.
- COM15 correctly rejected an over-dense Code 128 display at the requested minimum module size,
  then generated and displayed `COM15-OK` as a 123-module Code 128 symbol. Download returned 16
  bytes with CRC32 `393409740`.
- Both boards returned to a healthy ready state with approximately 37.8 KiB free heap after reset.

The first command sent immediately after a CH340-triggered cold reset was reproducibly lost because
the full TFT/LittleFS/BLE/ESP-NOW boot exceeded the old host delay. The .NET and Python transports now
wait 2.5 seconds and discard boot chatter before their first command. A reset-followed-immediately-by-
`hello` check passed after this correction, and the .NET gateway handshake then succeeded.

## Real Bluetooth LE checks

The Windows BLE connector exercised the same service, characteristics, 200-byte hop-frame profile,
fragmentation, envelope correlation, and commands used by Web Bluetooth in the PWA.

1. Connected to the first advertising display, completed `system.hello`, and displayed
   `BLE-HARDWARE-SCREEN-1` as a `21x21` QR symbol.
2. Held that connection open so the first board stopped advertising.
3. Connected a second BLE session, necessarily selecting the other physical board, completed
   `system.hello`, and displayed `BLE-HARDWARE-SCREEN-2` as a `21x21` QR symbol.

Both responses reported `displayed=true`. Browser security intentionally prevents Playwright from
automating Chrome's real chooser, so the browser UI is covered by the byte-accurate fake-BLE E2E
suite while this test supplies real GATT/radio/firmware evidence.

## Real secure gateway checks

Both boards already had Secure Pairing enabled and mutually persisted trust. After resetting both:

1. COM7 entered gateway mode through the corrected .NET legacy-to-COBS handshake.
2. `gateway.ping.now` discovered COM15 over the air; the peer record contained the expected MAC and
   device id.
3. A trusted reconnect was started for COM15. It progressed from `discovering` to `committed`
   without human confirmation, as required for an already-trusted static key, installing a fresh LMK.
4. The host stamped COM15's persisted route id (`1`) into hop frames.
5. `system.hello` completed over USB → gateway → encrypted ESP-NOW → client and back.
6. `barcode.generate` displayed `USB-ESPNOW-HARDWARE-ROUNDTRIP` on COM15 and returned
   `displayed=true`, `21x21`.
7. The final peer snapshot reported both `via_ping=true` and `via_relay=true`, with 22 ms discovery RTT.

This proves the new host-side per-peer route plumbing against the real firmware rather than only the
browser emulator.

## Automated gates

- Release .NET solution build: zero warnings and errors.
- .NET tests: Client 20, Connectivity 14, Protocol 15, Generator 106, Viewer CLI 35.
- Playwright/Reqnroll browser suite: 25 scenarios, including BLE fan-out, photo import, PWA mobile
  layout/installability, wired gateway use, and strict route-id targeting.
- PlatformIO native: 25/25.
- ESP32 cross-build: passed.

## Remaining physical acceptance boundary

No claim is made here for scanner optics, touch calibration by observation, camera-based decoding in
Chrome on a physical Android handset, hour-long soak behavior, or power-loss testing. Those require a
human observer and the target scanner/phone; follow `docs/HARDWARE_ACCEPTANCE.md` for release-level
optical qualification.
