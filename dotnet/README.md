# EspBarcode .NET client

A .NET 10 / C# 14 implementation of the [USB serial protocol](../docs/PROTOCOL.md)
client, alongside the Python one in `tools/espbarcode.py`.

```
dotnet/
  EspScreenBarcodeGenerator.slnx
  src/
    EspBarcode.Client/       Protocol client library (System.IO.Ports transport, NDJSON framing, raw-matrix packing/CRC32)
    EspBarcode.Cli/          Console demo app driving common test scenarios
    EspBarcode.Generator/    Host-side barcode generation/layout/PNG rendering library (no device needed)
    EspBarcode.Viewer.Cli/   Standalone generate/open/close CLI (file, OS image viewer, or the viewer window)
    EspBarcode.Viewer.Gui/   WPF viewer window + embedded Kestrel loopback server, launched/driven by EspBarcode.Viewer.Cli
    EspBarcode.Controller.Web/  Installable Blazor PWA — mobile BLE, Web Serial, and ESP-NOW gateway control (see below)
  tests/
    EspBarcode.Client.Tests/      xUnit tests covering the protocol client and the PDF417 "license" scenario
    EspBarcode.Generator.Tests/   xUnit tests covering encoders, layout, and PNG rendering
    EspBarcode.Viewer.Cli.Tests/  xUnit tests covering CLI argument parsing and viewer IPC
    EspBarcode.Controller.Web.E2ETests/  Reqnroll (Gherkin) + Playwright BDD suite driving the browser controller end to end
```

## Build & test

```powershell
cd dotnet
dotnet test EspScreenBarcodeGenerator.slnx
```

The test suite runs entirely against a scripted fake transport — no hardware required.

## Running the demo CLI against real hardware

```powershell
cd dotnet
dotnet run --project src/EspBarcode.Cli -- list-ports
dotnet run --project src/EspBarcode.Cli -- demo --port COM7
```

Individual scenarios:

```powershell
dotnet run --project src/EspBarcode.Cli -- hello --port COM7
dotnet run --project src/EspBarcode.Cli -- qr --port COM7 --data "LAB-TEST-001"
dotnet run --project src/EspBarcode.Cli -- upc --port COM7 --data "03600029145"
dotnet run --project src/EspBarcode.Cli -- code128 --port COM7 --data "LOT-2026-00042"
dotnet run --project src/EspBarcode.Cli -- license --port COM7
```

`--port` can also come from the `ESP_BARCODE_PORT` environment variable.

The `license` scenario is the interesting one: the firmware doesn't generate PDF417
on-board (see the main README's "Current encoder limits"), so it demonstrates the
documented workaround — [ZXing.Net](https://github.com/micjahn/ZXing.Net) renders a
PDF417 module matrix for a synthetic AAMVA-style driver's-license payload on the host,
and `EspBarcodeClient.UploadRawMatrix` pushes it to the device through the
`upload_begin`/`upload_chunk`/`upload_end` raw-matrix protocol, CRC32-validated end to end.

## Standalone generator/viewer (no ESP required)

`EspBarcode.Generator`, `EspBarcode.Viewer.Cli`, and `EspBarcode.Viewer.Gui`
replicate the ESP's barcode generation and display behavior entirely on
the host — no device connection needed. This is the tool to reach for
when testing a scanner against a screen without flashed hardware handy.

```powershell
cd dotnet
dotnet run --project src/EspBarcode.Viewer.Cli -- generate qr "LAB-TEST-001" --out qr.png
dotnet run --project src/EspBarcode.Viewer.Cli -- generate code128 "LOT-2026-00042" --open system
dotnet run --project src/EspBarcode.Viewer.Cli -- generate datamatrix "DM-TEST" --open viewer
dotnet run --project src/EspBarcode.Viewer.Cli -- close
```

`--open viewer` launches (or reuses) `EspBarcode.Viewer.Gui`, a plain
resizable window a scanner can read directly off the screen — the same
workflow the ESP's own DISPLAY button supports, without the device. The
window live-relayouts on resize, and repeated `generate ... --open viewer`
calls update the same window instead of opening a new one.

Supports every symbology the ESP does (`espbarcode-viewer list-types`)
plus host-only PDF417. Aztec Rune (`--aztec-layers 0`) is not supported —
see the design spec at
`docs/superpowers/specs/2026-08-20-dotnet-standalone-viewer-design.md`
for why.

`--rotation` accepts `auto` (default) plus all four explicit orientations
`0`/`90`/`180`/`270`, each producing a genuinely distinct placement using
the firmware's own per-module formulas (`src/BarcodeApplication.cpp`,
`renderCurrent`). `auto` weighs `0` against `90` and takes whichever
yields the larger module size, matching the firmware's `calculateLayout`.

`--min-module` is clamped to a minimum of 1 pixel per module, again
matching the firmware; a symbol that cannot reach that minimum on the
target canvas fails with `too_dense` rather than rendering blank.

### Viewer HTTP API

The GUI hosts a loopback-only HTTP server on `127.0.0.1:<port>` (default
`47823`, overridable with `--port` / `ESP_BARCODE_VIEWER_PORT`). The CLI
drives it, but so can any test orchestrator:

| Method | Path      | Body                | Response |
| ------ | --------- | ------------------- | -------- |
| GET    | `/health` | —                   | `200` when the viewer is up |
| POST   | `/render` | `BarcodeSpec` JSON  | `200`, or `400` `{"code","message"}` on an invalid spec, or `500` `{"code","message"}` on an unexpected render failure |
| POST   | `/close`  | —                   | `200`, then the viewer exits |

`/render`'s body is the `BarcodeSpec` record in camelCase, mirroring the
`generate` command's fields one-for-one (see
[`docs/PROTOCOL.md`](../docs/PROTOCOL.md)). `type` is the **same
wire-value string used everywhere else in this project** — `qr`,
`datamatrix`, `aztec`, `code128`, `gs1-128`, `code39`, `upca`, `ean13`,
`ean8`, `itf`, `itf14`, `codabar`, `msi`, `pdf417` — not an enum ordinal:

```json
{
  "type": "qr",
  "data": "LAB-TEST-001",
  "ecc": "M",
  "rotation": "auto",
  "quiet": -1,
  "minModule": 2,
  "rectangular": false,
  "invert": false,
  "checksum": true,
  "qrMinVersion": 1,
  "qrMaxVersion": 20,
  "aztecSecurity": 23,
  "aztecLayers": 1
}
```

Only `type` and `data` are required; every other field defaults exactly as
the `generate` command's corresponding option does.

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:47823/render `
  -ContentType application/json -Body '{"type":"qr","data":"TEST"}'
```

### Dev-mode note: locating the viewer executable

`--open viewer` looks for `EspBarcode.Viewer.Gui.exe` next to
`EspBarcode.Viewer.Cli`'s own executable — the layout a published
release ships in (both apps' output copied into one folder). Running
from source with `dotnet run --project src/EspBarcode.Viewer.Cli`,
that folder only contains the CLI's own build output, not the GUI's, so
the auto-detected path won't exist yet. Build the GUI project first,
then point the CLI at its output with `--viewer-exe` or the
`ESP_BARCODE_VIEWER_EXE_PATH` environment variable:

```powershell
dotnet build src/EspBarcode.Viewer.Gui --configuration Debug
dotnet run --project src/EspBarcode.Viewer.Cli -- generate qr "LAB-TEST-001" --open viewer `
  --viewer-exe src/EspBarcode.Viewer.Gui/bin/Debug/net10.0-windows/EspBarcode.Viewer.Gui.exe
```

(or `$env:ESP_BARCODE_VIEWER_EXE_PATH = "..."` once per shell session
instead of repeating `--viewer-exe` on every command).

## Mobile/browser control panel (`EspBarcode.Controller.Web`)

A standalone, installable Blazor WebAssembly PWA that connects to one or more ESP screens
from Android Chrome over Web Bluetooth, or from a Chromium browser over the [Web Serial API](https://developer.mozilla.org/docs/Web/API/Web_Serial_API)
— no native client, install, or driver beyond a Chromium-based browser
(Chrome/Edge; Web Serial isn't implemented in Firefox/Safari). It speaks the
same NDJSON v1 protocol as `EspBarcode.Client` (reimplemented on top of a
JS-interop byte stream instead of `System.IO.Ports`, since WASM can't use
that), plus the EspLink v2 subset a gateway-mode board relays
(`system.hello`/`barcode.generate`) via the existing carrier-agnostic
`EspBarcode.Protocol`/`EspBarcode.Connectivity` libraries.

The production build is hosted at [ESP Barcode Control](https://jerrettdavis.github.io/EspScreenBarcodeGenerator/).

```powershell
dotnet run --project src/EspBarcode.Controller.Web
```

For a phone, deploy the published `wwwroot` behind HTTPS (Web Bluetooth requires a secure context),
open it in Chrome on Android, and use **Install app** from Chrome's menu. iOS browsers do not expose
Web Bluetooth. Local desktop development may use the printed `http://localhost:...` URL. Pages:
**Dashboard** (fleet overview), **Devices** (pair/monitor/reboot/orientation/
gateway-mode entry, nearby BLE picker to connect/disconnect wireless screens — a plain client
board also shows its own ESP-NOW gateway-discovery status, see below), **Generator** (build a
barcode, camera/gallery barcode import, multi-screen targeting across wired, Bluetooth, and
individually routed gateway/paired-peer targets, push to one or more devices at once, live
preview downloaded from wired targets), **Library** (browser-local saved specs + each device's on-board LittleFS
presets), **Gateway** (drive the EspLink v2 relay once a board is in gateway
mode, including its discovered/relayed ESP-NOW peers), **Automation** (Full
Auto Mode: unattended reconnect/poll/playlist rotation), and **Settings**
(light/dark theme — same palette as `lib/UiGeometry/src/Theme.h`, so the
browser app and the physical screens match).

The two-board hardware evidence for direct serial, two simultaneous BLE sessions, and an encrypted,
route-addressed USB→ESP-NOW gateway round trip is recorded in
[`docs/validation/mobile-hardware-2026-08-25.md`](../docs/validation/mobile-hardware-2026-08-25.md).

### Gateway ↔ client discovery ping/pong

A gateway-mode board and any plain client board can find each other over
ESP-NOW without a host in the loop, using a new `ServiceId::Gateway`
(`gateway.link.ping`/`gateway.link.pong`) broadcast exchange — either role can
initiate. The Gateway page's **"Ping for Clients"** button asks the gateway
board to broadcast a discovery ping immediately (it also does this on its own
every ~2s); **"Refresh Peers"** re-reads its live peer list — MAC/device
id/RTT/last-seen, tagged by whether a peer was seen via real relayed traffic,
discovery ping, or both. Both `gateway.peers.list` and `gateway.ping.now` are
answered by the gateway board itself over USB (never relayed across
ESP-NOW), a deliberate, narrow exception to `GatewayRelay`'s usual
protocol-blind pass-through. A plain client board runs the other half
continuously in the background (no host command needed) and reports its own
"have I seen a gateway?" state through the ordinary `status` command's new
`gateway_link` field, shown on the Devices page. The same indicators exist
on the physical TFT screens: the Gateway stats screen gets a **PING** button
and per-peer RTT, and a plain client's Settings screen gets a small
"ESP-NOW Gateway: searching…/connected" row.

### E2E tests (`EspBarcode.Controller.Web.E2ETests`)

```powershell
dotnet test tests/EspBarcode.Controller.Web.E2ETests
```

Gherkin features under `Features/` (mobile Bluetooth broadcast, image import, unified gateway targeting,
device connection, generation, storage, gateway relay, Full Auto Mode, theme) run via Reqnroll + Playwright against a
real dev-server instance and a real headless Chromium. Since Web Serial
requires a live physical device picker no automation can click through, the
suite injects `wwwroot/js/fakeSerial.js` — a software ESP emulator (NDJSON v1
**and** a byte-accurate EspLink v2 COBS/hop-frame/envelope implementation) as
`window.__espFakeSerial` before the app boots. `fakeBluetooth.js` adapts that same emulator to BLE's
one-hop-frame-per-GATT-message contract, so the whole stack, gateway
relay included, gets deterministic, hardware-free coverage. A real two-board
hardware pass still needs a human to complete the one-time browser device
picker (Web Serial's security model has no programmatic bypass); everything
else — protocol framing, generation, presets, automation — was exercised
against real ESP32 boards for this feature's hardware validation.

## Library usage

```csharp
using EspBarcode.Client;

using var client = EspBarcodeClient.Connect("COM7");

var hello = client.Hello();
Console.WriteLine($"{hello.Device} firmware {hello.Firmware}");

var result = client.Generate(new GenerateOptions
{
    Type = BarcodeType.Qr,
    Data = "LAB-TEST-001",
});
Console.WriteLine($"{result.Width}x{result.Height} modules, normalized={result.NormalizedData}");
```

`EspBarcodeClient` takes an `IEspBarcodeTransport`, so it can be driven by a fake
transport in tests without a real serial port — see
`tests/EspBarcode.Client.Tests/FakeTransport.cs`.
