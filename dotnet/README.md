# EspBarcode .NET client

A .NET 10 / C# 14 implementation of the [USB serial protocol](../docs/PROTOCOL.md)
client, alongside the Python one in `tools/espbarcode.py`.

```
dotnet/
  EspScreenBarcodeGenerator.slnx
  src/
    EspBarcode.Client/   Protocol client library (System.IO.Ports transport, NDJSON framing, raw-matrix packing/CRC32)
    EspBarcode.Cli/      Console demo app driving common test scenarios
  tests/
    EspBarcode.Client.Tests/   xUnit tests covering the protocol client and the PDF417 "license" scenario
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
