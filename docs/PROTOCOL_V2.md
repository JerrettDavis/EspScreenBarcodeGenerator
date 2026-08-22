# EspLink Protocol v2

## 1. Overview

EspLink v2 is the canonical end-to-end control protocol for this device: one command, correlation, and error model that every carrier (USB, and eventually Bluetooth, Wi-Fi Direct, or an ESP-NOW gateway) is expected to carry unchanged. ESP-NOW itself is treated as one carrier among several, not as the protocol — the wire format does not assume Espressif's action-frame transport is present.

This release ships v2 over exactly one carrier: USB serial, opt-in via an explicit upgrade from the v1 line protocol. See [`docs/PROTOCOL.md`](PROTOCOL.md) for the still-default, still-fully-supported USB Serial Protocol 1.0 (newline-delimited JSON) — v1 is not deprecated by this document, and firmware boots into it by default.

## 2. Layering

Four layers, matching the design plan's layering (`docs/superpowers/plans/2026-08-20-multi-transport-esp-link-implementation-plan.md` §8.2), with the real types that implement each one in this repo:

```text
Layer 4: Domain body
  JSON control body: {"schema":"esbg.control/2.0","name":...,"body":{...}}
  Firmware: JsonCommandCodec (src/JsonCommandCodec.h) — no Serial dependency
  .NET:     EspBarcode.Connectivity.Client.EspLinkControlSession

Layer 3: End-to-end message envelope
  esplink::MessageEnvelope (lib/EspLinkCore/src/Envelope.h)
  EspBarcode.Protocol.MessageEnvelope (dotnet/src/EspBarcode.Protocol/MessageEnvelope.cs)

Layer 2: Per-hop EspLink frame
  esplink::HopFrameHeader (lib/EspLinkCore/src/HopFrame.h) + esplink::FrameAssembler
  EspBarcode.Protocol.HopFrameHeader + FrameAssembler

Layer 1: Carrier framing
  esplink::Cobs (lib/EspLinkCore/src/Cobs.h) — COBS + 0x00 delimiter over the same
  UART the v1 endpoint already uses
  EspBarcode.Protocol.Cobs (dotnet/src/EspBarcode.Protocol/Cobs.cs)
```

`lib/EspLinkCore` has zero Arduino dependency (`lib/EspLinkCore/library.json`: "No Arduino dependency"). `JsonCommandCodec` links against ArduinoJson for its `JsonObjectConst`/`JsonDocument` types but never touches `Serial`. Only the two endpoint classes — `SerialLegacyEndpoint` and `SerialCobsEndpoint` — own the `Serial` object. This is why `tests/esplink_codec_tests.cpp`, `tests/esplink_golden_shape_tests.cpp`, `tests/esplink_selection_tests.cpp`, `tests/esplink_types_tests.cpp`, and `tests/control_protocol_engine_tests.cpp` all run as plain host binaries with no ESP32 attached.

## 3. Message envelope

32-byte fixed header, little-endian multi-byte fields, immediately followed by `bodyLength` bytes of body (`lib/EspLinkCore/src/Envelope.cpp`, `lib/EspLinkCore/src/Envelope.h`):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | `magic` | ASCII `EM` |
| 2 | 1 | `major` | `2` — decode rejects any other major version |
| 3 | 1 | `minor` | `0` |
| 4 | 1 | `kind` | `MessageKind` |
| 5 | 1 | `flags` | Reserved, currently unused (`0`) |
| 6 | 1 | `serviceId` | `ServiceId` |
| 7 | 1 | `codecId` | `CodecId` |
| 8 | 4 | `controlSessionId` | The `esplink::ControlSession` this message belongs to |
| 12 | 4 | `bodyLength` | Exact body byte count following the header |
| 16 | 8 | `operationId` | This message's own operation identity |
| 24 | 8 | `correlationId` | The operation being answered; `0` for a new command |

Total header size: 32 bytes (`kEnvelopeHeaderSize`).

`MessageKind` (`lib/EspLinkCore/src/ConnectivityTypes.h`):

| Value | Name |
|---:|---|
| 0 | `Command` |
| 1 | `Result` |
| 2 | `Event` |
| 3 | `Error` |
| 4 | `Transfer` |

`ServiceId`:

| Value | Name |
|---:|---|
| 0 | `System` |
| 1 | `Barcode` |
| 2 | `Preset` |
| 3 | `Transfer` |
| 4 | `Device` |
| 5 | `Connectivity` |
| 6 | `Trust` |
| 7 | `Gateway` |
| 8 | `Diagnostics` |

`CodecId`:

| Value | Name |
|---:|---|
| 0 | `Json` |
| 1 | `Binary` |

All three enums are marked wire-relevant in `ConnectivityTypes.h` ("do not renumber") because their numeric values appear directly in this header.

## 4. Hop frame

Each envelope-encoded Layer 3 message is wrapped in one or more per-hop frames before it goes on the wire (`lib/EspLinkCore/src/HopFrame.cpp`, `lib/EspLinkCore/src/HopFrame.h`):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 2 | `magic` | ASCII `EL` |
| 2 | 1 | `major` | `2` |
| 3 | 1 | `minor` | `0` |
| 4 | 1 | `frameType` | `FrameType` |
| 5 | 1 | `flags` | Reserved, currently `0` |
| 6 | 1 | `trafficClass` | `TrafficClass` |
| 7 | 1 | `profileId` | `CarrierProfileId` |
| 8 | 2 | `routeId` | `0x0000` for a direct link (the only route this session uses) |
| 10 | 2 | `headerLength` | `32`; decode rejects any other value |
| 12 | 4 | `linkSessionId` | Per-hop session id |
| 16 | 4 | `linkMessageId` | Logical Layer 3 message identity on this hop |
| 20 | 4 | `linkCorrelationId` | Reserved for hop-level ACK/NACK correlation (unused this release — no reliability layer is implemented yet) |
| 24 | 2 | `fragmentIndex` | Zero-based fragment index |
| 26 | 2 | `fragmentCount` | Total fragments for this Layer 3 message |
| 28 | 2 | `payloadLength` | Bytes in this fragment |
| 30 | 2 | `reserved` | Must be `0`; decode rejects a nonzero value |
| 32 | N | `payload` | Fragment of the Layer 3 message |
| 32 + N | 4 | `frameCrc32` | IEEE CRC-32 (poly `0xEDB88320`) over header + payload |

`rawFrameLength = 36 + payloadLength` (`kHopFrameOverhead = 36` = 32-byte header + 4-byte CRC trailer). CRC is verified before any reassembly or session state is touched; a bad CRC, bad magic, wrong `headerLength`, nonzero `reserved`, or an out-of-range `fragmentIndex`/`fragmentCount` all cause the decoder to reject the frame without exposing a partially-decoded value.

Route ID `0x0000` ("direct/current endpoint") is the only route this session produces or accepts — `SerialCobsEndpoint` always writes `routeId = 0` implicitly (the header's default) and never checks any other value. Gateway-assigned peer routes (`0x0001`–`0xFFFE`) and gateway-local management (`0xFFFF`) are reserved by the design but unimplemented; no gateway firmware exists yet (see §10).

Fragmentation and reassembly are already wired: `esplink::FrameAssembler` (firmware) and `EspBarcode.Protocol.FrameAssembler` (.NET) both track partial messages by `(linkSessionId, linkMessageId, routeId)` and return `AssemblyOutcome::Complete` once every fragment has arrived. Neither side has exercised a multi-fragment message against real hardware this session — every command and response sent so far fits in one fragment.

## 5. Carrier profiles

| Profile | Raw frame ceiling | Framing | Reliability | Intended use |
|---|---:|---|---|---|
| `Unspecified` (0) | — | — | — | Default/unset value; never sent on the wire |
| `EspNowV1` (1) | 250 | One raw frame per ESP-NOW datagram | Fire-and-forget (no app ACK/NACK yet) | Compatibility — implemented (`src/EspNowEndpoint.cpp`), hw-validated radio bring-up only (see §10) |
| `EspNowV2` (2) | 1,470 | One raw frame per ESP-NOW datagram | App ACK/NACK | Preferred ESP-NOW path (not implemented) |
| `StreamSmall` (3) | 1,024 | COBS + `0x00` | Ordered reliable stream | Conservative RFCOMM/USB (not implemented) |
| `StreamStandard` (4) | 4,096 | COBS + `0x00` | Ordered reliable stream | **Default and only profile this release's `SerialCobsEndpoint` negotiates** |
| `StreamLarge` (5) | 16,384 | Length prefix or COBS | Ordered reliable stream | Capable USB/TCP endpoints (not implemented) |
| `TcpStandard` (6) | 4,096 (firmware-advertised; protocol ceiling is 16,384) | 32-bit length prefix | TCP | Wi-Fi Direct TCP — implemented (`src/WifiDirectTcpEndpoint.cpp` + .NET `EspBarcode.Client.WifiDirect`); provisioning bootstrap hw-validated end-to-end, radio join not yet hw-validated, see §10 |
| `TcpLarge` (7) | 65,535 max, lower device cap | 32-bit length prefix | TCP | Optional host/gateway path (not implemented) |
| `BleGattV1` (8) | 200 (conservative; requested MTU is 247) | One raw frame per GATT write/notify | Fire-and-forget (no app ACK/NACK yet) | BLE GATT — implemented and hw-validated end-to-end (`src/BleGattEndpoint.cpp` + .NET `EspBarcode.Client.Ble`), see §10 |

**Known simplification:** `SerialCobsEndpoint::send`/`sendError` hardcode `header.profileId = CarrierProfileId::StreamStandard` and `body["carrier"]["maxFrameBytes"] = 4096` directly in the `system.welcome` response (`src/SerialCobsEndpoint.cpp`). There is no per-connection negotiation of frame ceiling, `maxInFlightFrames`, or reliability mode — the design plan's richer `system.hello`/`system.welcome` exchange (§8.10, capability lists, nonce, `maxTransferBytes`, `capabilitiesRevision`) is not implemented; this release's handshake only exchanges `deviceId`, `firmware`, `selectedVersion`, and `controlSessionId` in addition to the fixed carrier profile. `CarrierProfileId::Unspecified` is the struct default and is never intentionally placed on the wire.

## 6. v1-to-v2 negotiation

The device always boots into v1 (`SerialLegacyEndpoint`, NDJSON) — v2 is opt-in per connection:

1. Host sends the v1 request `{"cmd":"upgrade"}` (optionally with `id`) over the existing NDJSON line protocol.
2. Firmware (`SerialLegacyEndpoint::processLine`, `src/SerialLegacyEndpoint.cpp`) replies on the same NDJSON channel: `{"ok":true,"cmd":"upgrade","message":"switching to EspLink v2 COBS framing"}`, flushes `Serial`, and sets `upgradeRequested_ = true`.
3. `main.cpp`'s `loop()` checks `legacyEndpoint.upgradeRequested()` after each `legacyEndpoint.loop()` call; once true it stops calling the legacy endpoint and starts calling `cobsEndpoint.loop()` instead. The switch is a one-way, one-time transition per boot — there is no downgrade path back to v1 without a reboot.
4. On the host, `EspBarcode.Client.TransportV2.UpgradeHandshake.RequestUpgrade` sends that same `upgrade` request through the existing v1 `EspBarcodeClient`/`SerialPortTransport`, then `SerialV2Connector.ConnectAsync` hands the *same, still-open* `SerialPort` to a new `SerialLinkConnection` (`dotnet/src/EspBarcode.Client/TransportV2/`). No port close/reopen happens.
5. From that point, both sides speak COBS-framed hop frames. The first Layer 3 message the host sends is `system.hello`; the firmware answers with `system.welcome` (§7).

The firmware never re-checks for `upgrade` once it's in COBS mode, and the v2 endpoint has no path back to NDJSON — the upgrade is real-hardware-confirmed working but is a single one-way handshake, not a general renegotiation protocol.

## 7. v2 command subset (this release)

`SerialCobsEndpoint` maps exactly five v2 command names to existing v1 handlers via `ControlProtocolEngine` (`src/SerialCobsEndpoint.cpp`'s file-local `mapV2Name`):

| v2 command name | Maps to v1 handler | Notes |
|---|---|---|
| `system.hello` | `hello` | Response is renamed to `system.welcome` and gains `carrier`/`controlSessionId` fields (§6) |
| `system.ping` | `hello` | This release has no distinct `ping` handler; `ping` reuses the `hello` response shape |
| `barcode.generate` | `generate` | |
| `barcode.close` | `close` | |
| `device.backlight.set` | `backlight` | |
| `device.orientation.set` | `orientation` | |

Every other v2 name in the design plan's §8.9 namespaces — `system.capabilities`, `system.status`, `system.close`, `system.reboot`, `system.resume`, all of `barcode.display`/`barcode.home`/`barcode.current`, all of `preset.*`, all of `transfer.*`, `device.screen.wake`/`sleep`/`info`, all of `connectivity.*`, `trust.*`, `gateway.*`, and `diagnostics.*` — is **not implemented over v2** this release. `SerialCobsEndpoint::processMessage` returns `{"error":{"code":"unknown_command","message":"command not supported over EspLink v2 this release"}}` for any name that doesn't match the five above. The full v1 command set (18 commands, including presets, upload/download, and reboot) remains available by staying on v1 or by talking to the same board without sending `upgrade`.

Responses/errors are single-fragment only (`fragmentIndex = 0`, `fragmentCount = 1`); a response body that would exceed one hop frame's payload budget is silently dropped rather than fragmented (a known gap, see §10).

## 8. Extension guide: adding a transport

1. Implement a new carrier connector: on .NET, `ILinkConnector`/`ILinkConnection` (`dotnet/src/EspBarcode.Connectivity/Client/ILinkConnection.cs`) around the new carrier's raw byte I/O — `SerialV2Connector`/`SerialLinkConnection` are the reference implementation, and `InMemoryDuplexConnection` is a minimal loopback example used by tests. On firmware, a new `*Endpoint` class analogous to `SerialCobsEndpoint`, implementing `IControlResponseSink` and driving the same `ControlProtocolEngine`.
2. Do not touch `ControlProtocolEngine`, `JsonCommandCodec`, or the envelope/hop-frame codecs (`Envelope.h`/`HopFrame.h`/`Cobs.h` and their .NET equivalents) — they are already carrier-agnostic. A new transport only ever supplies bytes in and bytes out; everything above Layer 1 is reused unchanged.
3. If the carrier's frame ceiling is smaller than the largest v1/v2 message this repo currently sends, add fragmentation at the connector boundary using the existing `FrameAssembler` and the hop frame's `fragmentIndex`/`fragmentCount` fields — no new wire format is needed. (Neither side currently emits `fragmentCount > 1`; the assembler and fields already exist for when a transport needs it.)
4. Add a new `CarrierProfileId` entry only if the carrier needs a negotiated frame ceiling not already in the §5 table (e.g. a real ESP-NOW or TCP profile) — do not repurpose an existing value, since the enum is wire-relevant.

## 9. Migration notes from `UsbProtocol`

`UsbProtocol` (the pre-Task-2 monolithic v1 dispatcher) was replaced by `JsonCommandCodec` + `SerialLegacyEndpoint` running over `ControlProtocolEngine` (`refactor(firmware): replace UsbProtocol with JsonCommandCodec + SerialLegacyEndpoint over ControlProtocolEngine`, commit `2c768a2`). The v1 wire format in `docs/PROTOCOL.md` did not change; only the internal decomposition did:

| Old (`UsbProtocol`) | New | Notes |
|---|---|---|
| `UsbProtocol::UploadState` | `esplink::TransferSession` (`lib/EspLinkCore/src/TransferSession.h`) | Now session-scoped: each `ControlSession` owns its own `TransferSession`, so a v1 and a v2 session (if both existed concurrently) could not corrupt each other's in-progress upload |
| `UsbProtocol::dispatch` | `ControlProtocolEngine::dispatchSingle` | Typed `Command`/`Response` variants instead of ad hoc JSON branching; catalog-driven (`commandCatalogName`) |
| `UsbProtocol::send` / `sendOk` / `sendError` | `IControlResponseSink` (`send`/`sendError`) | Each endpoint (`SerialLegacyEndpoint`, `SerialCobsEndpoint`) implements the sink for its own wire format instead of the engine writing to `Serial` directly |
| `UsbProtocol::parseSpec` | `JsonCommandCodec::parseSpec` | Same merge-with-active-spec algorithm, but now takes the base `BarcodeSpec` as an explicit parameter instead of reaching into a stored `application_` reference |

## 10. Next PRs

### .NET `FallbackPolicy` parity (near-term, small)

Firmware's `lib/EspLinkCore/src/SelectionPolicy.h` has `FallbackPolicy` (`UnavailableOnly`, `ConnectFailure`, `PreOperation`, `Never`) and a pure `isFallbackAllowed(FallbackPolicy, FallbackContext)` function, covered by 5 of `tests/esplink_selection_tests.cpp`'s 13 scenarios. `dotnet/src/EspBarcode.Connectivity/TransportSelector.cs` and `ConnectivityTypes.cs` have no C# equivalent of either type. This was a deliberate scope cut for this session (only one transport exists on either side, so there is nothing to fall back from yet), not an oversight — but it is a real gap a future PR should close before any second transport lands on the .NET side, so the fallback decision logic isn't reimplemented ad hoc per connector.

### Bluetooth RFCOMM

Implement `ILinkConnector`/`ILinkConnection` (.NET, WinRT RFCOMM) and a `BluetoothSppEndpoint` (firmware, `BluetoothSerial`) around the existing `ControlProtocolEngine`/envelope/frame codecs from this plan's firmware and .NET foundation work — the same seam described in §8. Add `TransportKind::Bluetooth`/`CapabilityState::Available` wiring to `SelectionPolicy`'s capability probing (`SelectionPolicy.h` and `TransportSelector.cs` already declare `TransportKind::Bluetooth` and score it — only the connector and capability probe are missing). Production pairing/trust (design plan §7.7 — `TrustRecord`, `PairingSession`, key rotation) is out of scope through this entire plan; `ControlSession`'s current lease is a minimal single-holder flag (`tryAcquireLease`/`releaseLease`/`hasLease` in `lib/EspLinkCore/src/ControlSession.h`), not the full `ControlLease` aggregate (`LeaseId`/`ExpiresAt`/`Permissions`/`RevocationReason`) design plan §7.6 describes — that gap is a seam for future multi-controller work, appropriate for now because only one transport/session exists.

### Wi-Fi Direct legacy GO + TCP (implemented; provisioning bootstrap hw-validated, radio join not yet hw-validated)

`src/WifiDirectTcpEndpoint.cpp` implements the ESP-side half: the display runs as a normal Wi-Fi station (`WiFi.begin`), dials a `WiFiClient` TCP connection to `WiFi.gatewayIP()` (the Windows legacy group owner is the DHCP gateway by construction) on a configured port, and speaks EspLink v2 over the `TcpStandard` carrier — `uint32_le length + raw hop frame`, implemented as a new, carrier-agnostic `LengthPrefixFrameParser`/`encodeLengthPrefixedFrame` pair (`lib/EspLinkCore/src/LengthPrefixFraming.h`, unit-tested by `tests/esplink_length_prefix_framing_tests.cpp` — byte-at-a-time feeds, multi-frame-per-read, zero/oversized-length rejection, and a 4 KiB frame split across TCP-MSS-sized chunks). It reuses `ControlProtocolEngine`/`Envelope`/`HopFrame`/`FrameAssembler`/`Fragmenter` unchanged, exactly like every other v2 endpoint.

Unlike the passive-callback endpoints (BLE, ESP-NOW), this one owns a real connection-management state machine (`Idle → WifiConnecting → TcpConnecting → Connected`, with timeouts and a fixed reconnect backoff) because Wi-Fi association and a TCP handshake are both asynchronous, retryable operations — `loop()` drives that state machine every tick in addition to pumping the socket once connected.

**Credential provisioning** (design plan §12.6 "Trusted Bluetooth bootstrap"): the display never has WiFi Direct credentials compiled in. `BleGattEndpoint` special-cases a `device.wifiDirect.configure` command (bypassing `ControlProtocolEngine` — it's device provisioning, not a barcode/domain operation) that forwards `{ssid, passphrase, port}` to `WifiDirectTcpEndpoint::configure`, which persists them to NVS (`WifiDirectProvisioning`, `Preferences` namespace `"wifidirect"`) and immediately (re)starts the connect state machine. Any already-trusted transport could host this bootstrap step; BLE is the one wired up because it's this codebase's only transport with real device-in-hand pairing today.

On Windows, `dotnet/src/EspBarcode.Client.WifiDirect` (new project, `net10.0-windows10.0.19041.0`, WinRT `Windows.Devices.WiFiDirect` + `Windows.Networking.Sockets`) provides `WifiDirectLegacyConnector`: starts a `WiFiDirectAdvertisementPublisher` legacy group (generating an ephemeral SSID/passphrase if none is supplied), binds a `StreamSocketListener` on the configured port, and accepts the display's incoming TCP connection. Because the connection is already exactly-one-message-per-read/write once `WifiDirectTcpLinkConnection` strips the length prefix, it reuses `EspLinkDatagramLinkSession` unchanged — no new session type was needed here, unlike BLE.

**What's hardware-validated end-to-end**: the entire BLE provisioning round trip, for real, against the flashed esp32dev board — a scratch console harness started a `WifiDirectLegacyConnector` (real ephemeral SSID/passphrase), connected over BLE (the already-validated transport), sent `device.wifiDirect.configure` with those exact values, and got back `{"ok":true}`; serial diagnostics on the device confirmed `WifiDirectTcpEndpoint::configure` was invoked, persisted the credentials, and entered `WifiConnecting`. The .NET solution (all 15 projects) and the native `esplink_length_prefix_framing_tests`/full ctest suite (8 suites) both pass.

**What's blocked, and why, precisely**: the actual Wi-Fi radio join was **not** validated end-to-end on this development machine. `WiFiDirectAdvertisementPublisher.Start()` reports `Status = Started` successfully — confirmed callable from this repo's unpackaged console apps with no package-identity error — but on this PC's Intel Wi-Fi 6E AX211, no access point is ever actually radiated: `Get-NetAdapter` shows no new virtual Wi-Fi Direct adapter appearing, the primary Wi-Fi adapter stays `Disconnected` throughout, and the `Microsoft-Windows-WLAN-AutoConfig/Operational` event log shows zero driver activity for the entire publisher lifetime. The ESP32 side, correctly, reports `wl_status_t = WL_NO_SSID_AVAIL` (status 1) on every scan — it genuinely cannot see a network that was never broadcast. This is a documented, real limitation of `WiFiDirectLegacySettings` on at least some Intel Wi-Fi driver stacks (the WinRT API accepts the request and reports success without the driver implementing the feature) — not a bug in this endpoint or connector. `WifiDirectLegacyConnector.AcceptConnectionAsync` now distinguishes this from a generic cancellation with an actionable `TimeoutException` pointing at the `Get-NetAdapter`/event-log diagnostic. A future session should retry this exact validation on a Wi-Fi adapter/driver known to support legacy group-owner mode (Realtek and Broadcom chipsets have historically been more reliable here than Intel) before treating the radio-join path as hardware-proven.

**Known scope cuts vs. the full §12 design**, consistent with this release's other transports: `WifiDirectLegacyConnector`'s TCP listener binds all local interfaces rather than only the Wi-Fi Direct group's virtual adapter (§12.4 item 7 / §12.7 network-boundary requirement) — WinRT's legacy-GO API doesn't expose that adapter's id directly, and finding it reliably needs more investigation; no capability probe distinguishing "adapter doesn't support legacy GO" from "policy blocked it" from "device never joined" (§12.4's required failure taxonomy) beyond the timeout message above; no radio-arbitration state machine between Wi-Fi Direct and ESP-NOW (§12.12 — both would fight over the same STA-mode radio/channel today, since `WifiDirectTcpEndpoint` calls `WiFi.begin` unconditionally once configured); `WiFiClient::connect()` is blocking, bounded in practice to the TCP-handshake latency of a direct single-hop link, rather than the fully non-blocking connect design plan §12.4 implies; and no persisted-profile management UX (§12.6) beyond the raw NVS save/load.

### BLE GATT display endpoint + Windows connector (implemented, hw-validated end-to-end)

`src/BleGattEndpoint.cpp` implements the new `BleGattV1` compatibility profile (§5) using the ESP32 Arduino `BLEDevice`/`BLEServer`/`BLECharacteristic` API: one service (`6f6d7501-...-5a01`) with a write characteristic (host→device commands) and a notify characteristic (device→host results/errors). Like `EspNowEndpoint`, each GATT write/notify *is* one raw hop frame — no COBS. It reuses `ControlProtocolEngine`/`Envelope`/`HopFrame`/`FrameAssembler`/`Fragmenter` unchanged and runs unconditionally alongside the serial and ESP-NOW endpoints (`main.cpp`'s `loop()` calls `bleEndpoint.loop()` every iteration).

On Windows, `dotnet/src/EspBarcode.Client.Ble` (new project, `net10.0-windows10.0.19041.0`, WinRT `Windows.Devices.Bluetooth`) provides `BleGattConnector`/`BleGattLinkConnection`. Building this surfaced a real gap in the existing .NET session layer: `EspLinkLinkSession` unconditionally COBS-encodes/decodes, which is wrong for a datagram carrier where each `ILinkConnection.ReadAsync`/`WriteAsync` call is already exactly one message. Rather than bolt a "skip COBS" flag onto `EspLinkLinkSession`, a sibling `EspLinkDatagramLinkSession` (`dotnet/src/EspBarcode.Connectivity/Client/`) was added with the matching contract instead — same public shape, no COBS, its own fragmentation loop mirroring firmware's `Fragmenter`. Any future datagram carrier (a Windows-side ESP-NOW gateway connector, once the gateway firmware exists) should reuse this session type, not `EspLinkLinkSession`.

**Hardware-validated end-to-end**: a throwaway console harness (not committed — built against `BleGattConnector` in a scratch directory) scanned for, connected to, and exchanged `system.hello`/`system.welcome` and a real `barcode.generate` (rendered on-device) with the flashed esp32dev board over this PC's actual Bluetooth radio — the strongest validation any transport in this repo has had, since it exercises both firmware and Windows connector against real hardware and a real OS Bluetooth stack in one round trip (ESP-NOW, by contrast, only got single-board radio-init validation — see previous section).

**A debugging note worth keeping**: `BluetoothLEAdvertisementWatcher` delivers the primary advertisement PDU (service UUIDs) and the scan-response PDU (local name) as *separate* `Received` events for the same device — a filter that requires both `ServiceUuids` and `LocalName` to match in the same event will never fire. `BleGattConnector.ScanForDeviceAsync` matches on either signal alone for exactly this reason.

**Known scope cuts**, matching ESP-NOW: peer trust is "any connected central" (no pairing/authentication — design plan §7.7 remains out of scope), no application-level ACK/NACK/retry, and the requested MTU (247) is best-effort — a central that negotiates a smaller MTU than expected will silently fail to receive any frame at all, since `kHopFrameOverhead` (36 bytes) already exceeds the default unnegotiated ATT payload (20 bytes). This worked against Windows' BLE stack this session; it has not been tried against Linux (BlueZ) or macOS (CoreBluetooth), and neither has any connector been written for those platforms — `EspBarcode.Client.Ble` is Windows-only (WinRT). A cross-platform BLE connector would need a different library (e.g., a BlueZ D-Bus binding for Linux, CoreBluetooth interop for macOS) behind the same `ILinkConnector`/`ILinkConnection` seam; not attempted this session.

### ESP-NOW display endpoint (implemented, partially validated)

`src/EspNowEndpoint.cpp` implements the `EspNowV1` compatibility profile on the display board itself: `IControlResponseSink` over native `esp_now`, reusing `ControlProtocolEngine`, `Envelope`, `HopFrame`, and `FrameAssembler` unchanged, plus a new portable helper (`lib/EspLinkCore/src/Fragmenter.h`, covered by `tests/esplink_fragmenter_tests.cpp`) that splits an outgoing envelope across as many 250-byte ESP-NOW datagrams as needed — the send-side counterpart to the fragmentation the extension guide (§8, point 3) already called for. It runs unconditionally alongside whichever serial transport is active (`main.cpp`'s `loop()` calls `espNowEndpoint.loop()` every iteration) since the ESP-NOW radio doesn't contend with UART.

What's confirmed: the radio initializes on real hardware (`{"event":"espnow_ready","mac":"..."}` on boot, hw-validated on the esp32dev board) and the fragmentation/reassembly math is unit-tested. **A real two-peer ESP-NOW exchange over the air is now hardware-validated** (a second board became available this session): a Windows client, talking to one board acting as the [gateway](#esp-now-usb-gateway-implemented), sent `system.hello` and `barcode.generate` (display=true) over USB→ESP-NOW broadcast→the second board's `EspNowEndpoint`, which processed them via the same `ControlProtocolEngine` every other transport uses and returned a correctly correlated `system.welcome`/`generate` response relayed back over ESP-NOW→USB. The barcode rendered on the second board's screen (`"displayed":true`). Repeated runs show occasional single-command drops (never a systemic failure) — expected given this profile's documented fire-and-forget behavior (no application-level ACK/retry yet, §8.13 gap noted below), not a framing or dispatch bug.

Current scope cuts, matching the rest of this release: peer trust is a single hardcoded broadcast-address peer (no pairing, no encryption — design plan §7.7 trust/pairing remains entirely out of scope), and there is no application-level ACK/NACK or retry (the design plan's "App ACK/NACK" reliability requirement for this profile, §8.13, is not implemented — `onEspNowSent` is a no-op).

### ESP-NOW USB gateway (implemented)

Rather than a second PlatformIO project, the gateway is a runtime mode of the *same* firmware image: any board can act as either a normal client/display or the USB↔ESP-NOW gateway, selected per boot session. It boots as a client (today's full `BarcodeApplication` + all transports) and stays that way unless a host explicitly requests gateway mode over the legacy USB line — there is no cable-presence auto-detection (this board's CH340 USB-serial bridge exposes no reliable "a terminal is open" signal), so the switch is request-driven only, exactly like the existing `upgrade` handshake.

- **Handshake**: `SerialLegacyEndpoint` gains a `gateway` command (`src/SerialLegacyEndpoint.cpp`), handled identically to `upgrade` — ack immediately, then flip a one-way flag `main.cpp`'s `loop()` checks. Like `upgrade`, there is no revert to client mode without a reboot.
- **Relay**: `src/GatewayRelay.cpp` is a pure Layer-2 (hop-frame) bridge — it never touches `ControlProtocolEngine`, `JsonCommandCodec`, or parsed envelope/JSON bodies, per the design plan's "a gateway may decode Layer 2, preserve Layer 3 bytes" rule (§8.4). It runs two `FrameAssembler`s (one per inbound leg); on a completed reassembly it re-fragments the same opaque envelope bytes for the other carrier's frame-size ceiling via `Fragmenter::fragmentIntoHopFrames`. `linkSessionId`/`trafficClass`/`routeId` pass through unchanged (`lib/EspLinkCore/src/GatewayBridge.h`'s `relayHeaderFor`, unit-tested in `tests/esplink_fragmenter_tests.cpp` including a full USB→ESP-NOW→reassembly round trip); `linkMessageId` gets a fresh counter per outbound leg; `profileId` swaps to the destination carrier. It reuses the ESP-NOW radio `EspNowEndpoint::begin()` already initialized at boot (channel, broadcast peer) — activating gateway mode only re-registers the `esp_now` receive callback and stops `main.cpp` from calling `espNowEndpoint.loop()`.
- **`.NET` side**: no new interface or byte-stream handling needed — `GatewaySerialConnector`/`GatewayHandshake` (`dotnet/src/EspBarcode.Client/TransportV2/`) mirror `SerialV2Connector`/`UpgradeHandshake` exactly, just sending `{"cmd":"gateway"}` instead of `{"cmd":"upgrade"}`. The wire framing on the far side of that handshake — COBS-delimited EspLink v2 hop frames — is identical either way; only what's at the other end of those frames differs (this board's own dispatch vs. a relayed display board).

**Hardware-validated** on two real ESP32 boards: `system.hello` and `barcode.generate`(display=true) round-tripped through the gateway over ESP-NOW to the second board and back, with the barcode actually rendering. Getting there surfaced and fixed one real bug (not a "known gap" — an actual defect, now fixed): `SerialLegacyEndpoint::loop()`'s `while (Serial.available() > 0)` kept draining bytes across multiple lines in one call, so if a host sent COBS-framed bytes for the *new* mode immediately after receiving the `upgrade`/`gateway` ack (no artificial delay), those bytes could arrive before `main.cpp`'s next `loop()` iteration flips `active` — and get consumed as corrupt legacy JSON-line content by this same `while` loop instead of being left for the new endpoint. Fixed by returning from `loop()` immediately once `upgradeRequested()`/`gatewayRequested()` is set, leaving any already-buffered bytes for the new endpoint's own `loop()` call right after. This also applies to (and fixes) the pre-existing `upgrade` → `SerialCobsEndpoint` handshake, not just the new `gateway` one.

Two boards were also found with a latent, unrelated interaction: one had a stale Wi-Fi Direct credential persisted in NVS from an earlier session (§12), which made `WifiDirectTcpEndpoint::loop()` call `WiFi.begin()` on every boot — silently pulling the radio off ESP-NOW's fixed channel 1 and making ESP-NOW un-usable on that board until the NVS was cleared. Worth a note for whoever hits this again: `WifiDirectTcpEndpoint` and `EspNowEndpoint`/`GatewayRelay` share one Wi-Fi radio with no mutual awareness — provisioning Wi-Fi Direct on a board currently doing serious ESP-NOW work will break the ESP-NOW work. Not fixed this session (out of scope); flagging as a real gap for whoever picks up Wi-Fi Direct + ESP-NOW coexistence.

### Known cleanup (not a "next PR" on its own, fold into whichever PR touches `SerialCobsEndpoint` next)

`src/SerialCobsEndpoint.h` declares `writeMessage`, `writeMessageWithResponse`, and `v1NameFor` as private members. None are defined in `src/SerialCobsEndpoint.cpp` — the actual send path is inlined directly in `send()`/`sendError()`, and the actual name mapping is the free function `mapV2Name` in an anonymous namespace. These three declarations are dead leftovers from an earlier design sketch; harmless (nothing calls them, so the linker never needs a definition) but worth deleting the next time this file is touched.
