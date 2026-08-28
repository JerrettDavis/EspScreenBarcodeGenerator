# Devices/Generator unification — design

Date: 2026-08-27
Status: Approved

## Problem

`EspBarcode.Controller.Web` (the Blazor PWA that controls ESP barcode
screens) currently spreads "connect to a screen" and "put a barcode on a
screen" across three pages that don't agree with each other:

- **Devices** — wired (Web Serial) connections only. Its "Target Devices"
  concept doesn't exist here; this page is purely about connection
  lifecycle (connect/reconnect/disconnect/forget) and per-device actions
  (backlight, reboot, orientation, entering Gateway Mode).
- **Wireless** — a second, parallel connection surface for Bluetooth
  devices, plus its *own* copy of the barcode-spec form and its *own*
  target-selection logic that (uniquely) already knows how to address all
  three kinds of screen this app can reach: wired Client devices, wired
  Gateway-relay boards' ESP-NOW peers, and Bluetooth devices.
- **Generator** — the primary barcode-authoring screen, but its "Target
  Devices" list only ever shows wired Client-mode devices. It has no idea
  Bluetooth or ESP-NOW-peer targets exist, even though Wireless's send
  logic proves they're reachable.

This means: to push a barcode to a Bluetooth screen, or to an ESP-NOW peer
behind a gateway, a user has to leave Generator (whose form is the most
complete) and re-enter the same barcode data into Wireless's smaller,
duplicate form. There is no single "author a barcode, pick any connected
screen, send it" flow.

Separately, Generator's post-send preview (`DownloadCurrentMatrixAsync` +
canvas render — a pixel-exact mirror of what the physical screen is
showing) only works for wired Client-mode devices today, because that
download command only exists in the v1 wire protocol
(`EspDeviceClient`). Bluetooth and gateway-relay-peer targets speak the v2
protocol (`GatewayLinkClient`), which this release implements only 5
commands for (`system.hello`, `barcode.generate`, `barcode.close`,
`device.backlight.set`, `device.orientation.set` — see
`docs/PROTOCOL_V2.md` §7) and has no equivalent download command at all.

## Scope note: split from the protocol work

Making v2 targets show a *real* bitmap preview requires new firmware +
protocol work (see "Non-goals" below) — genuinely separate, cross-repo
work with its own hardware-validation requirements. This spec covers only
the Blazor-side unification, designed so that follow-up work is additive
(no rework of this spec's pieces).

## Non-goals

- **No v2 `barcode.download` protocol command in this pass.** Bluetooth
  and gateway-relay-peer targets keep using their existing
  `barcode.generate` (push + physically display) — this spec does not
  touch firmware, `docs/PROTOCOL_V2.md`, or add commands to
  `ControlProtocolEngine`/`JsonCommandCodec`/`SerialCobsEndpoint`. That
  command is planned as a follow-up sub-project: two new 1:1
  request/response v2 commands (`barcode.download.begin` returning
  metadata, `barcode.download.chunk` pulling fixed-size slices sized to
  the connection's frame ceiling), reusing the existing v1 handler's
  matrix-packing/CRC32 logic, and *not* reusing the v1 `download` command
  as-is (it streams three push-events per request, which the v2 client
  session's strict 1-command-in/1-response-out `SendCommandAsync` cannot
  receive).
- **No full unification of `DeviceConnection` and `BluetoothDevice`
  into one model/registry.** `DeviceRegistry` and `BluetoothDeviceRegistry`
  stay two separate services with their own device types. Devices.razor
  renders both device lists together on one page; nothing else about
  either registry changes.
- **No changes to Home.razor's dashboard stats or the topbar's
  connected-device badge.** Both read `DeviceRegistry` only today and
  keep doing so — "Connected Devices" stays wired-only. Extending those to
  count Bluetooth devices is a reasonable follow-up, not bundled here
  since it wasn't asked for and changes a stat's meaning.
- **No new device actions for Bluetooth targets** (backlight, reboot,
  orientation). `GatewayLinkClient` doesn't expose backlight/orientation
  yet even though the v2 protocol subset technically carries those two
  commands, and `device.reboot` isn't in the v2 subset at all. Bluetooth
  device cards get exactly what Wireless.razor already exposed:
  connection status and Disconnect.
- **No change to Gateway.razor.** It remains the deep-dive page for
  gateway peer discovery, trust/pairing, and per-peer relay generation —
  explicitly kept as-is per the request.

## Architecture

### New service: `GenerationTargetProvider`

A new type in `Services/GenerationTarget.cs`, constructed from the
existing `DeviceRegistry` and `BluetoothDeviceRegistry` (both already
registered in `Program.cs`; no DI changes beyond adding this one new
scoped/singleton service — same lifetime as the two registries it wraps).

```csharp
public enum GenerationTargetKind { Wired, GatewayPeer, Bluetooth }

public sealed record GenerationTarget(
    string Id,
    string DisplayName,
    GenerationTargetKind Kind,
    Func<GenerateOptions, CancellationToken, Task<GenerateResult>> SendAsync,
    Func<CancellationToken, Task<DownloadedMatrix>>? DownloadPreviewAsync);

public sealed class GenerationTargetProvider(DeviceRegistry wired, BluetoothDeviceRegistry bluetooth)
{
    // Wired Client-mode devices + Bluetooth devices: always available, no round trip.
    public IReadOnlyList<GenerationTarget> GetImmediateTargets();

    // Wired Gateway-relay devices themselves, as today's Wireless.razor "wired:{id}"
    // target (routeId 0) — same existing semantics, not redesigned here.
    public GenerationTarget GetGatewayDirectTarget(DeviceConnection gateway);

    // One gateway device's trusted ESP-NOW peers, addressed by routeId — the caller
    // (Generator.razor) is still the one deciding *when* to fetch/refresh this list,
    // exactly like Wireless.razor's "Refresh Paired Gateway Screens" button today.
    public IReadOnlyList<GenerationTarget> BuildPeerTargets(DeviceConnection gateway, IReadOnlyList<TrustedPeer> peers);
}
```

`DownloadPreviewAsync` is populated only for `Kind == Wired` targets right
now (`() => device.Client.DownloadCurrentMatrixAsync(...)`) — `null` for
`GatewayPeer` and `Bluetooth`. This is the deliberate seam: the follow-up
protocol work only has to make `GatewayLinkClient` grow an equivalent
method and populate this field for the other two kinds. No caller of
`GenerationTarget` needs to change.

This provider is a straight extraction of logic that already exists,
today, inlined in `Wireless.razor`'s `SendAsync`/`Toggle`/`_targets`
handling — it is not new behavior, just centralized so Generator can use
it too instead of Generator and Wireless each growing their own copy.

### Devices.razor: Bluetooth as a second connection mode

- Inject `BluetoothDeviceRegistry`.
- Top card gets a second button, "+ Add Bluetooth Screen", next to
  "+ Connect Device" — same `IsSupportedAsync()`/unsupported-browser alert
  Wireless.razor already has (`data-testid="ble-unsupported"`,
  `data-testid="connect-bluetooth"`, both reused as-is).
- The device grid renders wired cards (unchanged) followed by Bluetooth
  cards. A Bluetooth card shows: name (no nickname editing — `BluetoothDevice`
  has no persisted-nickname concept today, matching current behavior), a
  "Bluetooth" badge in place of Port/Client/Gateway-relay, firmware
  version, and Disconnect. No Refresh/Backlight/Reboot/Forget/orientation
  controls (see Non-goals).
- Empty-state copy updates to mention both connection options.

### Generator.razor: unified targets, moved photo import, graceful preview

- Replace the `ClientDevices` getter with `GenerationTargetProvider`-backed
  target construction: `GetImmediateTargets()` (wired Client + Bluetooth)
  plus, per connected Gateway-relay wired device, a "Refresh Paired Gateway
  Screens" button (`GetGatewayDirectTarget` + `BuildPeerTargets` after an
  explicit `ListTrustedPeersAsync()` round trip) — this is Wireless.razor's
  existing `_gatewayTargets` dictionary pattern, moved here unchanged.
  Peer targets render indented under their gateway, exactly like
  Wireless.razor's `esb-peer-target` rows.
- Each target in the checklist shows its `DisplayName` plus a small kind
  tag (Wired / Bluetooth / Gateway peer) so the operator knows which link
  a given checkbox uses.
- "Import from photo" (camera capture → `BarcodeImportService.DecodeAsync`
  → fills `Data`/`Type`) moves from Wireless.razor into the Barcode Spec
  card, gated on `Importer.IsSupportedAsync()`, same as today.
- "Generate & Push" iterates the selected `GenerationTarget`s and calls
  each one's `SendAsync` (replacing the current direct
  `device.Client.GenerateAsync` loop). Dispatch stays sequential per
  target, matching today's wired-only loop — Wireless.razor's
  `Task.WhenAll` parallelization was specific to its Bluetooth-only send
  path; there's no correctness requirement to preserve that here, and
  sequential keeps the failure-reporting loop simple. If send latency
  with many Bluetooth targets turns out to matter, revisit later.
- After a push, the preview panel prefers the first successful target's
  `DownloadPreviewAsync` when present (real bitmap, wired targets only,
  same canvas render as today); when the first successful target has none
  (Bluetooth/gateway-peer), it shows the text result summary — "W×H
  modules · normalized "..."" — the same information Gateway.razor already
  displays, instead of leaving the preview panel blank.

### Removed: Wireless.razor

- `Pages/Wireless.razor` deleted.
- `Layout/MainLayout.razor`'s "📱 Wireless" nav link removed (this spec
  doesn't touch the nav icon styling itself — that's the separate, already
  -shipped visual-polish canvas).
- Nothing else references the `/wireless` route (`Program.cs`'s DI
  registrations for `BluetoothDeviceRegistry`/`WebBluetoothModule`/
  `WebBluetoothConnection` stay — Devices.razor and Generator.razor now
  depend on them instead).

## Testing

`Features/Wireless.feature` and `StepDefinitions/WirelessStepDefinitions.cs`
are retired. Their scenarios split by what they actually exercise:

- **Connection scenarios** (supported/unsupported browser, connect,
  disconnect, device list) move into `Features/DeviceConnection.feature`,
  reusing `connect-bluetooth`/`ble-unsupported`/`bluetooth-device`
  testids against Devices.razor instead of Wireless.razor.
- **Targeting/send scenarios** (selecting a mix of wired/Bluetooth/peer
  targets, sending, photo import, result reporting) move into
  `Features/Generator.feature`, reusing `wireless-type`/`wireless-data`
  -equivalent testids renamed to match Generator's existing
  `generator-*` naming convention.

The implementation plan (next step, via the writing-plans skill) enumerates
the exact scenario-by-scenario moves and any new unit tests for
`GenerationTargetProvider`.

## Rollout

Single PR, no feature flag — this is a page/nav restructuring with no
protocol or wire-format change, so there's no partial-deployment concern.
Existing hardware-validated generation behavior (wired, Bluetooth, gateway
relay) is preserved exactly; only the UI surface it's reached from moves.
