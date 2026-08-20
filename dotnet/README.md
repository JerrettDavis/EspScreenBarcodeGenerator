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
  tests/
    EspBarcode.Client.Tests/      xUnit tests covering the protocol client and the PDF417 "license" scenario
    EspBarcode.Generator.Tests/   xUnit tests covering encoders, layout, and PNG rendering
    EspBarcode.Viewer.Cli.Tests/  xUnit tests covering CLI argument parsing and viewer IPC
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
