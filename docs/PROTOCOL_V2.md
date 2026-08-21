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
| `EspNowV1` (1) | 250 | One raw frame per ESP-NOW datagram | App ACK/NACK | Compatibility (not implemented) |
| `EspNowV2` (2) | 1,470 | One raw frame per ESP-NOW datagram | App ACK/NACK | Preferred ESP-NOW path (not implemented) |
| `StreamSmall` (3) | 1,024 | COBS + `0x00` | Ordered reliable stream | Conservative RFCOMM/USB (not implemented) |
| `StreamStandard` (4) | 4,096 | COBS + `0x00` | Ordered reliable stream | **Default and only profile this release's `SerialCobsEndpoint` negotiates** |
| `StreamLarge` (5) | 16,384 | Length prefix or COBS | Ordered reliable stream | Capable USB/TCP endpoints (not implemented) |
| `TcpStandard` (6) | 16,384 | 32-bit length prefix | TCP | Wi-Fi Direct TCP (not implemented) |
| `TcpLarge` (7) | 65,535 max, lower device cap | 32-bit length prefix | TCP | Optional host/gateway path (not implemented) |

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

### Wi-Fi Direct legacy GO + TCP

Implement the same connector seam over a Windows-created group-owner TCP listener and an ESP32 Wi-Fi station endpoint. Reuse the `TcpStandard`/`TcpLarge` `CarrierProfileId` values already reserved in `lib/EspLinkCore/src/ConnectivityTypes.h` (§5) rather than adding new ones.

### ESP-NOW USB gateway

A second PlatformIO project (gateway firmware) bridging a USB `SerialCobsEndpoint`-like link to native ESP-NOW. The gateway re-fragments opaque `MessageEnvelope` bytes across the USB↔ESP-NOW frame-ceiling gap using the same `FrameAssembler`/hop-frame `fragmentIndex`/`fragmentCount` fields, per the design plan's "a gateway may decode Layer 2, preserve Layer 3 bytes" rule (§8.4) — no new envelope format, and the gateway must not parse or translate barcode-domain JSON bodies merely to reframe them.

### Known cleanup (not a "next PR" on its own, fold into whichever PR touches `SerialCobsEndpoint` next)

`src/SerialCobsEndpoint.h` declares `writeMessage`, `writeMessageWithResponse`, and `v1NameFor` as private members. None are defined in `src/SerialCobsEndpoint.cpp` — the actual send path is inlined directly in `send()`/`sendError()`, and the actual name mapping is the free function `mapV2Name` in an anonymous namespace. These three declarations are dead leftovers from an earlier design sketch; harmless (nothing calls them, so the linker never needs a definition) but worth deleting the next time this file is touched.
