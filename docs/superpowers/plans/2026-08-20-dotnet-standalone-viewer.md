# Standalone .NET Barcode Generator & Viewer Implementation Plan

> **Amendment (during Task 11 execution):** Tasks 6 (`ScreenFitLayout`) and
> 8 (`BarcodeImageRenderer`) as originally written did not special-case
> linear (1D) symbologies — `RawMatrix.Height` is always 1 for these, and
> using it directly for both scale-fit and per-module square rendering
> produces an unscannable 1-scale-pixel-tall hairline for 9 of the 14
> supported types (everything except Qr/DataMatrix/Aztec/Pdf417). The
> firmware itself avoids this by treating linear symbols as having a fixed
> **48-module conceptual bar height** for all layout/scale math and
> drawing bars that span the full `48 * scale` pixels tall, not `1 *
> scale` (`lib/EspBarcodeCore/src/EspBarcodeCore.cpp:150-187`,
> `src/BarcodeApplication.cpp:652-686`). Both tasks' code below is
> corrected in place to match: `ScreenFitLayout` substitutes 48 for
> whichever of Width/Height is exactly 1 (a safe linear-symbol detector,
> since no supported 2D symbol ever has a dimension of 1), and
> `BarcodeImageRenderer` draws linear symbols as column-wise (or, if
> rotated, row-wise) bars spanning that full conceptual height instead of
> per-cell squares.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a standalone .NET CLI + WPF viewer that generate and display
any of the ESP's barcode types without a connected device, sharing all
generation/layout logic through a new `EspBarcode.Generator` core library.

**Architecture:** Three new projects sibling to the existing
`EspBarcode.Client`/`EspBarcode.Cli`: `EspBarcode.Generator` (encoding,
check digits, screen-fit layout, PNG rendering — no UI/IPC),
`EspBarcode.Viewer.Cli` (arg parsing, file/system-viewer output, HTTP
client to the GUI), `EspBarcode.Viewer.Gui` (WPF window + embedded Kestrel
loopback server so the CLI/test orchestrators can push new barcodes into
an already-open window and have it live-relayout on resize).
`EspBarcode.Client`'s `RawMatrix` moves into `EspBarcode.Generator` so
there is exactly one module-matrix type in the repo.

**Tech Stack:** .NET 10 / C# 14, ZXing.Net 0.16.11, System.Drawing.Common
(PNG rendering), WPF (`net10.0-windows`), ASP.NET Core minimal API
(Kestrel, loopback only), xUnit.

**Spec:** `docs/superpowers/specs/2026-08-20-dotnet-standalone-viewer-design.md`

## Global Constraints

- Target framework `net10.0` (`net10.0-windows` for the GUI project only),
  `LangVersion` 14.0, nullable + implicit usings enabled, warnings as
  errors — all inherited from `dotnet/Directory.Build.props`; do not
  override those in new projects except where this plan says to.
- `EspBarcode.Generator` uses `System.Drawing.Common`, which is
  Windows-only at runtime. **Do not mark this at the assembly level** —
  an assembly-wide `[assembly: SupportedOSPlatform("windows")]` marks
  every public member of that assembly (including plain logic like
  `RawMatrix`) as platform-restricted for the CA1416 analyzer, which
  cascades into forcing every consumer (`EspBarcode.Client`,
  `EspBarcode.Cli`, both otherwise cross-platform-capable via
  `System.IO.Ports`) to also become Windows-only just to build under
  `TreatWarningsAsErrors` — an architecture decision this plan never
  intended and Task 1's review caught. Apply `[SupportedOSPlatform("windows")]`
  narrowly, at the class level, only on `BarcodeImageRenderer` (Task 8),
  the one type that actually calls into `System.Drawing`. Everything
  else in `EspBarcode.Generator` (`RawMatrix`, `BarcodeType`,
  `BarcodeSpec`, the encoders, `ScreenFitLayout`, `Checksums`,
  `PayloadSource`) has no Windows-specific API calls and must stay
  analyzer-clean without any platform attribute.
- `BarcodeSpec` field names/defaults/ranges must exactly match
  `docs/PROTOCOL.md`'s `generate` command fields (same option vocabulary
  across firmware, Python tool, `EspBarcode.Client`, and this new tool).
  Defaults: `Ecc="M"`, `Rotation="auto"`, `Quiet=-1`, `MinModule=2`,
  `Rectangular=false`, `Invert=false`, `Checksum=true`,
  `QrMinVersion=1`, `QrMaxVersion=20`, `AztecSecurity=23`,
  `AztecLayers=1`.
- Aztec Rune (`AztecLayers == 0`) is explicitly unsupported in v1 — throw
  `BarcodeGenerationException`, do not attempt to encode it. See spec
  amendment.
- `.github/workflows/dotnet-ci.yml` must run on `windows-latest` by the
  end of this plan (WPF project requirement) and `main` must be green
  after the final push.
- Follow existing repo conventions: hand-rolled CLI arg parsing (no
  `System.CommandLine` or similar), `AssemblyInfo.cs` with
  `InternalsVisibleTo` for the matching test project, xUnit test projects
  named `<Project>.Tests` referencing `coverlet.collector`,
  `Microsoft.NET.Test.Sdk`, `xunit`, `xunit.runner.visualstudio` at the
  same versions as `EspBarcode.Client.Tests.csproj`.
- Commit after every task (small, working-tree-clean commits), matching
  this repo's history style. Push to `main` only in the final task, after
  full solution build+test passes.

---

### Task 1: Scaffold `EspBarcode.Generator` and move `RawMatrix` into it

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/EspBarcode.Generator.csproj`
- Create: `dotnet/src/EspBarcode.Generator/AssemblyInfo.cs`
- Create: `dotnet/src/EspBarcode.Generator/RawMatrix.cs` (moved from `EspBarcode.Client`)
- Delete: `dotnet/src/EspBarcode.Client/RawMatrix.cs`
- Modify: `dotnet/src/EspBarcode.Client/EspBarcode.Client.csproj` (add `ProjectReference` to `EspBarcode.Generator`)
- Modify: `dotnet/src/EspBarcode.Client/EspBarcodeClient.cs` (add `using EspBarcode.Generator;`)
- Create: `dotnet/tests/EspBarcode.Generator.Tests/EspBarcode.Generator.Tests.csproj`
- Move: `dotnet/tests/EspBarcode.Client.Tests/RawMatrixTests.cs` → `dotnet/tests/EspBarcode.Generator.Tests/RawMatrixTests.cs` (namespace updated)
- Modify: `dotnet/EspScreenBarcodeGenerator.slnx` (add both new projects)

**Interfaces:**
- Produces: `EspBarcode.Generator.RawMatrix` — identical API to the old
  `EspBarcode.Client.RawMatrix` (`Width`, `Height`, indexer, `Pack()`,
  `Unpack(width, height, packed)`, `FromGrid(bool[,])`). Every later task
  in this plan that produces a module matrix returns this type.

- [ ] **Step 1: Create the `EspBarcode.Generator` project file**

```xml
<!-- dotnet/src/EspBarcode.Generator/EspBarcode.Generator.csproj -->
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <PackageId>EspBarcode.Generator</PackageId>
    <Description>Standalone barcode generation, screen-fit layout, and PNG rendering core shared by EspBarcode.Viewer.Cli and EspBarcode.Viewer.Gui.</Description>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="ZXing.Net" Version="0.16.11" />
    <PackageReference Include="System.Drawing.Common" Version="10.0.0" />
  </ItemGroup>

</Project>
```

- [ ] **Step 2: Create `AssemblyInfo.cs`**

```csharp
// dotnet/src/EspBarcode.Generator/AssemblyInfo.cs
using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("EspBarcode.Generator.Tests")]
```

No platform attribute here — see the amended Global Constraints note:
`RawMatrix` (this task) has no Windows-specific API calls, and marking
the whole assembly `SupportedOSPlatform("windows")` would force
`EspBarcode.Client`/`EspBarcode.Cli` to also become Windows-only just to
build under `TreatWarningsAsErrors`, which this task must not do. Only
`BarcodeImageRenderer` (Task 8) needs a `[SupportedOSPlatform("windows")]`
attribute, applied at the class level.

- [ ] **Step 3: Move `RawMatrix.cs`**

Copy `dotnet/src/EspBarcode.Client/RawMatrix.cs` to
`dotnet/src/EspBarcode.Generator/RawMatrix.cs`, changing only the
namespace line:

```csharp
namespace EspBarcode.Generator;
```

(Body unchanged — same `bool[,]` packing implementation.) Delete the
original `dotnet/src/EspBarcode.Client/RawMatrix.cs`.

- [ ] **Step 4: Point `EspBarcode.Client` at the new project**

In `dotnet/src/EspBarcode.Client/EspBarcode.Client.csproj`, add:

```xml
  <ItemGroup>
    <ProjectReference Include="..\EspBarcode.Generator\EspBarcode.Generator.csproj" />
  </ItemGroup>
```

In `dotnet/src/EspBarcode.Client/EspBarcodeClient.cs`, add
`using EspBarcode.Generator;` near the top (alongside the existing
`using EspBarcode.Client.Transport;`).

- [ ] **Step 5: Create the `EspBarcode.Generator.Tests` project**

```xml
<!-- dotnet/tests/EspBarcode.Generator.Tests/EspBarcode.Generator.Tests.csproj -->
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <IsPackable>false</IsPackable>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="coverlet.collector" Version="6.0.4" />
    <PackageReference Include="Microsoft.NET.Test.Sdk" Version="17.14.1" />
    <PackageReference Include="xunit" Version="2.9.3" />
    <PackageReference Include="xunit.runner.visualstudio" Version="3.1.4" />
  </ItemGroup>

  <ItemGroup>
    <Using Include="Xunit" />
  </ItemGroup>

  <ItemGroup>
    <ProjectReference Include="..\..\src\EspBarcode.Generator\EspBarcode.Generator.csproj" />
  </ItemGroup>

</Project>
```

- [ ] **Step 6: Move `RawMatrixTests.cs`**

Copy `dotnet/tests/EspBarcode.Client.Tests/RawMatrixTests.cs` to
`dotnet/tests/EspBarcode.Generator.Tests/RawMatrixTests.cs`, changing the
namespace line to `namespace EspBarcode.Generator.Tests;`. Delete the
original from `EspBarcode.Client.Tests`.

- [ ] **Step 7: Wire both new projects into the solution**

In `dotnet/EspScreenBarcodeGenerator.slnx`, add under `/src/`:

```xml
    <Project Path="src/EspBarcode.Generator/EspBarcode.Generator.csproj" />
```

and under `/tests/`:

```xml
    <Project Path="tests/EspBarcode.Generator.Tests/EspBarcode.Generator.Tests.csproj" />
```

- [ ] **Step 8: Build and run the full suite to confirm the move didn't break anything**

Run: `cd dotnet && dotnet test EspScreenBarcodeGenerator.slnx`
Expected: all projects restore/build, and `RawMatrixTests` (now in
`EspBarcode.Generator.Tests`) plus every existing `EspBarcode.Client.Tests`
test pass.

- [ ] **Step 9: Commit**

```bash
git add dotnet/
git commit -m "Move RawMatrix into new EspBarcode.Generator project

Scaffolds the shared core library for the standalone barcode
viewer/CLI and gives the repo a single module-matrix type instead of
one owned by the ESP-specific client."
```

---

### Task 2: Core types — `BarcodeType`, `BarcodeSpec`, exception, viewer protocol constants

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/BarcodeType.cs`
- Create: `dotnet/src/EspBarcode.Generator/BarcodeSpec.cs`
- Create: `dotnet/src/EspBarcode.Generator/BarcodeGenerationException.cs`
- Create: `dotnet/src/EspBarcode.Generator/ViewerProtocol.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/BarcodeTypeTests.cs`

**Interfaces:**
- Consumes: nothing (leaf types).
- Produces:
  - `BarcodeType` enum: `Qr, DataMatrix, Aztec, Code128, Gs1_128, Code39, UpcA, Ean13, Ean8, Itf, Itf14, Codabar, Msi, Pdf417`.
  - `BarcodeTypeExtensions.ToWireValue(this BarcodeType) : string` and
    `BarcodeTypeExtensions.ParseWireValue(string) : BarcodeType` (throws
    `BarcodeGenerationException` with code `"unknown_type"` on no match).
  - `BarcodeSpec` record with the fields/defaults listed in Global
    Constraints.
  - `BarcodeGenerationException(string code, string message) : Exception`
    with a `Code` property.
  - `ViewerProtocol` static class: `DefaultPort = 47823`,
    `HealthPath = "/health"`, `RenderPath = "/render"`,
    `ClosePath = "/close"`.

- [ ] **Step 1: Write the failing test for wire-value round trip**

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/BarcodeTypeTests.cs
namespace EspBarcode.Generator.Tests;

public class BarcodeTypeTests
{
    [Theory]
    [InlineData(BarcodeType.Qr, "qr")]
    [InlineData(BarcodeType.DataMatrix, "datamatrix")]
    [InlineData(BarcodeType.Aztec, "aztec")]
    [InlineData(BarcodeType.Code128, "code128")]
    [InlineData(BarcodeType.Gs1_128, "gs1-128")]
    [InlineData(BarcodeType.Code39, "code39")]
    [InlineData(BarcodeType.UpcA, "upca")]
    [InlineData(BarcodeType.Ean13, "ean13")]
    [InlineData(BarcodeType.Ean8, "ean8")]
    [InlineData(BarcodeType.Itf, "itf")]
    [InlineData(BarcodeType.Itf14, "itf14")]
    [InlineData(BarcodeType.Codabar, "codabar")]
    [InlineData(BarcodeType.Msi, "msi")]
    [InlineData(BarcodeType.Pdf417, "pdf417")]
    public void ToWireValue_And_ParseWireValue_RoundTrip(BarcodeType type, string wire)
    {
        Assert.Equal(wire, type.ToWireValue());
        Assert.Equal(type, BarcodeTypeExtensions.ParseWireValue(wire));
    }

    [Fact]
    public void ParseWireValue_UnknownValue_ThrowsWithUnknownTypeCode()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => BarcodeTypeExtensions.ParseWireValue("not-a-type"));
        Assert.Equal("unknown_type", ex.Code);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter BarcodeTypeTests`
Expected: FAIL — `BarcodeType`/`BarcodeGenerationException` don't exist yet.

- [ ] **Step 3: Implement `BarcodeGenerationException`**

```csharp
// dotnet/src/EspBarcode.Generator/BarcodeGenerationException.cs
namespace EspBarcode.Generator;

/// <summary>Thrown for any generation failure: invalid spec, unsupported combination, or symbol too dense for its target canvas.</summary>
public sealed class BarcodeGenerationException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
```

- [ ] **Step 4: Implement `BarcodeType`**

```csharp
// dotnet/src/EspBarcode.Generator/BarcodeType.cs
namespace EspBarcode.Generator;

/// <summary>Symbologies the standalone generator supports. Wire values match docs/PROTOCOL.md's generate.type field for the 13 shared with the ESP, plus pdf417 (host-only, matching the existing "license" scenario).</summary>
public enum BarcodeType
{
    Qr,
    DataMatrix,
    Aztec,
    Code128,
    Gs1_128,
    Code39,
    UpcA,
    Ean13,
    Ean8,
    Itf,
    Itf14,
    Codabar,
    Msi,
    Pdf417,
}

public static class BarcodeTypeExtensions
{
    public static string ToWireValue(this BarcodeType type) => type switch
    {
        BarcodeType.Qr => "qr",
        BarcodeType.DataMatrix => "datamatrix",
        BarcodeType.Aztec => "aztec",
        BarcodeType.Code128 => "code128",
        BarcodeType.Gs1_128 => "gs1-128",
        BarcodeType.Code39 => "code39",
        BarcodeType.UpcA => "upca",
        BarcodeType.Ean13 => "ean13",
        BarcodeType.Ean8 => "ean8",
        BarcodeType.Itf => "itf",
        BarcodeType.Itf14 => "itf14",
        BarcodeType.Codabar => "codabar",
        BarcodeType.Msi => "msi",
        BarcodeType.Pdf417 => "pdf417",
        _ => throw new ArgumentOutOfRangeException(nameof(type), type, "Unknown barcode type"),
    };

    public static BarcodeType ParseWireValue(string value) => value switch
    {
        "qr" => BarcodeType.Qr,
        "datamatrix" => BarcodeType.DataMatrix,
        "aztec" => BarcodeType.Aztec,
        "code128" => BarcodeType.Code128,
        "gs1-128" => BarcodeType.Gs1_128,
        "code39" => BarcodeType.Code39,
        "upca" => BarcodeType.UpcA,
        "ean13" => BarcodeType.Ean13,
        "ean8" => BarcodeType.Ean8,
        "itf" => BarcodeType.Itf,
        "itf14" => BarcodeType.Itf14,
        "codabar" => BarcodeType.Codabar,
        "msi" => BarcodeType.Msi,
        "pdf417" => BarcodeType.Pdf417,
        _ => throw new BarcodeGenerationException("unknown_type", $"Unknown barcode type '{value}'."),
    };
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter BarcodeTypeTests`
Expected: PASS

- [ ] **Step 6: Implement `BarcodeSpec` and `ViewerProtocol` (no dedicated test — exercised by every later task)**

```csharp
// dotnet/src/EspBarcode.Generator/BarcodeSpec.cs
namespace EspBarcode.Generator;

/// <summary>Mirrors docs/PROTOCOL.md's generate command fields one-to-one, so the option vocabulary is identical across firmware, Python tool, EspBarcode.Client, and this generator.</summary>
public sealed record BarcodeSpec
{
    public required BarcodeType Type { get; init; }
    public required string Data { get; init; }
    public string Ecc { get; init; } = "M";
    public string Rotation { get; init; } = "auto";
    public int Quiet { get; init; } = -1;
    public int MinModule { get; init; } = 2;
    public bool Rectangular { get; init; }
    public bool Invert { get; init; }
    public bool Checksum { get; init; } = true;
    public int QrMinVersion { get; init; } = 1;
    public int QrMaxVersion { get; init; } = 20;
    public int AztecSecurity { get; init; } = 23;
    public int AztecLayers { get; init; } = 1;
}
```

```csharp
// dotnet/src/EspBarcode.Generator/ViewerProtocol.cs
namespace EspBarcode.Generator;

/// <summary>Shared contract between EspBarcode.Viewer.Cli and EspBarcode.Viewer.Gui's loopback HTTP server.</summary>
public static class ViewerProtocol
{
    public const int DefaultPort = 47823;
    public const string HealthPath = "/health";
    public const string RenderPath = "/render";
    public const string ClosePath = "/close";
}
```

- [ ] **Step 7: Build the whole solution**

Run: `cd dotnet && dotnet build EspScreenBarcodeGenerator.slnx`
Expected: builds clean (warnings-as-errors, so any nullable/analyzer issue surfaces now).

- [ ] **Step 8: Commit**

```bash
git add dotnet/
git commit -m "Add BarcodeType, BarcodeSpec, exception, and viewer protocol constants"
```

---

### Task 3: Check-digit and token-normalization helpers (`Checksums`)

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/Checksums.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/ChecksumsTests.cs`

**Interfaces:**
- Consumes: nothing.
- Produces (all `internal static` on `Checksums`, used by Task 4/5's
  encoders):
  - `Gs1CheckDigit(string digitsWithoutCheck) : int`
  - `LuhnCheckDigit(string digits) : int`
  - `NormalizeUpcA(string data, bool checksum) : string` (11→12 or validate 12)
  - `NormalizeEan13(string data, bool checksum) : string` (12→13 or validate 13)
  - `NormalizeEan8(string data, bool checksum) : string` (7→8 or validate 8)
  - `NormalizeItf14(string data, bool checksum) : string` (13→14 or validate 14)
  - `NormalizeItf(string data) : string` (digits only, left-pad `0` if odd length)
  - `NormalizeMsi(string data, bool checksum) : string` (digits only; appends Luhn digit if `checksum`)
  - `NormalizeCodabar(string data) : string` (adds `A`/`A` start/stop if the first/last char isn't one of `A-D`, case-insensitive)
  - `NormalizeFnc1Tokens(string data) : string` (replaces `{FNC1}`, `<FNC1>`, `<GS>`, and literal ASCII GS (``) with ZXing's `ñ` FNC1 escape char)

- [ ] **Step 1: Write the failing tests (hand-verified check-digit vectors)**

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/ChecksumsTests.cs
namespace EspBarcode.Generator.Tests;

public class ChecksumsTests
{
    [Fact]
    public void NormalizeUpcA_ElevenDigits_ComputesCheckDigit()
    {
        // Known-valid UPC-A: 036000291452 (Tic Tac, commonly cited example)
        Assert.Equal("036000291452", Checksums.NormalizeUpcA("03600029145", checksum: true));
    }

    [Fact]
    public void NormalizeUpcA_TwelveDigits_ValidatesExistingCheckDigit()
    {
        Assert.Equal("036000291452", Checksums.NormalizeUpcA("036000291452", checksum: true));
    }

    [Fact]
    public void NormalizeUpcA_TwelveDigits_WrongCheckDigit_Throws()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => Checksums.NormalizeUpcA("036000291459", checksum: true));
        Assert.Equal("invalid_checksum", ex.Code);
    }

    [Fact]
    public void NormalizeEan13_TwelveDigits_ComputesCheckDigit()
    {
        // Wikipedia's canonical EAN-13 example: 4006381333931
        Assert.Equal("4006381333931", Checksums.NormalizeEan13("400638133393", checksum: true));
    }

    [Fact]
    public void NormalizeEan8_SevenDigits_ComputesCheckDigit()
    {
        // Wikipedia's canonical EAN-8 example: 96385074
        Assert.Equal("96385074", Checksums.NormalizeEan8("9638507", checksum: true));
    }

    [Fact]
    public void NormalizeItf14_ThirteenDigits_ComputesCheckDigit()
    {
        Assert.Equal("12345678901231", Checksums.NormalizeItf14("1234567890123", checksum: true));
    }

    [Fact]
    public void NormalizeItf_OddLength_LeftPadsZero()
    {
        Assert.Equal("0123", Checksums.NormalizeItf("123"));
        Assert.Equal("1234", Checksums.NormalizeItf("1234"));
    }

    [Fact]
    public void NormalizeMsi_ChecksumTrue_AppendsLuhnDigit()
    {
        // Hand-computed Luhn digit for 1234567 is 4.
        Assert.Equal("12345674", Checksums.NormalizeMsi("1234567", checksum: true));
    }

    [Fact]
    public void NormalizeMsi_ChecksumFalse_LeavesDataUnchanged()
    {
        Assert.Equal("1234567", Checksums.NormalizeMsi("1234567", checksum: false));
    }

    [Theory]
    [InlineData("123", "A123A")]
    [InlineData("A123B", "A123B")]
    [InlineData("a123b", "a123b")]
    public void NormalizeCodabar_AddsStartStopWhenOmitted(string input, string expected)
    {
        Assert.Equal(expected, Checksums.NormalizeCodabar(input));
    }

    [Theory]
    [InlineData("{FNC1}010", "ñ010")]
    [InlineData("<FNC1>010", "ñ010")]
    [InlineData("<GS>010", "ñ010")]
    [InlineData("010", "ñ010")]
    public void NormalizeFnc1Tokens_ReplacesAllRecognizedTokens(string input, string expected)
    {
        Assert.Equal(expected, Checksums.NormalizeFnc1Tokens(input));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter ChecksumsTests`
Expected: FAIL — `Checksums` doesn't exist yet.

- [ ] **Step 3: Implement `Checksums`**

```csharp
// dotnet/src/EspBarcode.Generator/Checksums.cs
using System.Text;

namespace EspBarcode.Generator;

/// <summary>Check-digit computation/validation and FNC1 token handling shared by the retail (UPC/EAN/ITF-14/MSI) and Code128/GS1-128 encoders.</summary>
internal static class Checksums
{
    public static int Gs1CheckDigit(string digitsWithoutCheck)
    {
        var sum = 0;
        for (var i = 0; i < digitsWithoutCheck.Length; i++)
        {
            var digit = digitsWithoutCheck[digitsWithoutCheck.Length - 1 - i] - '0';
            var weight = i % 2 == 0 ? 3 : 1;
            sum += digit * weight;
        }
        return (10 - sum % 10) % 10;
    }

    public static int LuhnCheckDigit(string digits)
    {
        var sum = 0;
        var doubleIt = true;
        for (var i = digits.Length - 1; i >= 0; i--)
        {
            var d = digits[i] - '0';
            if (doubleIt)
            {
                d *= 2;
                if (d > 9) d -= 9;
            }
            sum += d;
            doubleIt = !doubleIt;
        }
        return (10 - sum % 10) % 10;
    }

    private static void RequireDigits(string data, string what)
    {
        if (data.Length == 0 || !data.All(char.IsAsciiDigit))
            throw new BarcodeGenerationException("invalid_payload", $"{what} requires digits only, got '{data}'.");
    }

    private static string NormalizeGs1Family(string data, int shortLength, int fullLength, bool checksum, string what)
    {
        RequireDigits(data, what);
        if (data.Length == shortLength)
        {
            return data + Gs1CheckDigit(data);
        }
        if (data.Length == fullLength)
        {
            if (!checksum) return data;
            var body = data[..shortLength];
            var expected = Gs1CheckDigit(body);
            if (data[^1] - '0' != expected)
                throw new BarcodeGenerationException("invalid_checksum", $"{what} check digit mismatch: '{data}' expected digit {expected}.");
            return data;
        }
        throw new BarcodeGenerationException("invalid_payload", $"{what} requires {shortLength} or {fullLength} digits, got {data.Length}.");
    }

    public static string NormalizeUpcA(string data, bool checksum) => NormalizeGs1Family(data, 11, 12, checksum, "UPC-A");
    public static string NormalizeEan13(string data, bool checksum) => NormalizeGs1Family(data, 12, 13, checksum, "EAN-13");
    public static string NormalizeEan8(string data, bool checksum) => NormalizeGs1Family(data, 7, 8, checksum, "EAN-8");
    public static string NormalizeItf14(string data, bool checksum) => NormalizeGs1Family(data, 13, 14, checksum, "ITF-14");

    public static string NormalizeItf(string data)
    {
        RequireDigits(data, "ITF");
        return data.Length % 2 == 0 ? data : "0" + data;
    }

    public static string NormalizeMsi(string data, bool checksum)
    {
        RequireDigits(data, "MSI");
        return checksum ? data + LuhnCheckDigit(data) : data;
    }

    public static string NormalizeCodabar(string data)
    {
        if (data.Length == 0) throw new BarcodeGenerationException("invalid_payload", "Codabar payload must not be empty.");
        var startOk = "ABCDabcd".IndexOf(data[0]) >= 0;
        var stopOk = "ABCDabcd".IndexOf(data[^1]) >= 0;
        if (startOk && stopOk) return data;
        return "A" + data + "A";
    }

    public static string NormalizeFnc1Tokens(string data)
    {
        var sb = new StringBuilder(data);
        sb.Replace("{FNC1}", "ñ").Replace("<FNC1>", "ñ").Replace("<GS>", "ñ").Replace("", "ñ");
        return sb.ToString();
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter ChecksumsTests`
Expected: PASS (all 13 cases)

- [ ] **Step 5: Commit**

```bash
git add dotnet/
git commit -m "Add Checksums: GS1/Luhn check digits and FNC1 token normalization"
```

---

### Task 4: Linear/checksum-family encoders (UPC-A, EAN-13, EAN-8, ITF, ITF-14, MSI, Code 39, Codabar)

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/Encoding/LinearEncoders.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/LinearEncodersTests.cs`

**Interfaces:**
- Consumes: `BarcodeSpec`, `Checksums.*` (Task 3), `RawMatrix` (Task 1).
- Produces: `internal static class LinearEncoders` with one method per
  symbology, each `(BarcodeSpec spec) -> RawMatrix`:
  `EncodeUpcA`, `EncodeEan13`, `EncodeEan8`, `EncodeItf`, `EncodeItf14`,
  `EncodeMsi`, `EncodeCode39`, `EncodeCodabar`. Each normalizes payload via
  `Checksums`, then calls the matching ZXing.Net writer with
  `EncodeHintType.MARGIN = 0` (quiet zone is applied later by
  `ScreenFitLayout`, Task 7 — not baked in here) and converts the
  resulting `BitMatrix` to `RawMatrix` via `RawMatrix.FromGrid`.

- [ ] **Step 1: Write the failing round-trip tests**

Each test encodes, then decodes the `RawMatrix` back through ZXing.Net's
own `BarcodeReader` (converting the matrix to a `BitMatrix` the reader can
read) and asserts the decoded text matches — this avoids hand-computing
expected bit patterns for every symbology.

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/LinearEncodersTests.cs
using EspBarcode.Generator.Encoding;
using ZXing;
using ZXing.Common;

namespace EspBarcode.Generator.Tests;

public class LinearEncodersTests
{
    private static string Decode(RawMatrix matrix, BarcodeFormat format)
    {
        var bits = new BitMatrix(matrix.Width, matrix.Height);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                if (matrix[x, y]) bits[x, y] = true;

        var reader = new ZXing.OneD.MultiFormatOneDReader(new Dictionary<DecodeHintType, object>
        {
            [DecodeHintType.POSSIBLE_FORMATS] = new List<BarcodeFormat> { format },
        });
        var luminance = new RGBLuminanceSourceFromBitMatrix(bits);
        var binarizer = new ZXing.Common.HybridBinarizer(luminance);
        var binaryBitmap = new BinaryBitmap(binarizer);
        var result = reader.decode(binaryBitmap);
        Assert.NotNull(result);
        return result!.Text;
    }

    [Fact]
    public void EncodeUpcA_ElevenDigitInput_RoundTripsToNormalizedTwelveDigits()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.UpcA, Data = "03600029145" };
        var matrix = LinearEncoders.EncodeUpcA(spec);
        Assert.Equal("036000291452", Decode(matrix, BarcodeFormat.UPC_A));
    }

    [Fact]
    public void EncodeEan13_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Ean13, Data = "400638133393" };
        var matrix = LinearEncoders.EncodeEan13(spec);
        Assert.Equal("4006381333931", Decode(matrix, BarcodeFormat.EAN_13));
    }

    [Fact]
    public void EncodeEan8_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Ean8, Data = "9638507" };
        var matrix = LinearEncoders.EncodeEan8(spec);
        Assert.Equal("96385074", Decode(matrix, BarcodeFormat.EAN_8));
    }

    [Fact]
    public void EncodeItf_OddLength_PadsAndRoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Itf, Data = "12345" };
        var matrix = LinearEncoders.EncodeItf(spec);
        Assert.Equal("012345", Decode(matrix, BarcodeFormat.ITF));
    }

    [Fact]
    public void EncodeItf14_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Itf14, Data = "1234567890123" };
        var matrix = LinearEncoders.EncodeItf14(spec);
        Assert.Equal("12345678901231", Decode(matrix, BarcodeFormat.ITF));
    }

    [Fact]
    public void EncodeCode39_UppercasesAndRoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Code39, Data = "lot-2026" };
        var matrix = LinearEncoders.EncodeCode39(spec);
        Assert.Equal("LOT-2026", Decode(matrix, BarcodeFormat.CODE_39));
    }

    [Fact]
    public void EncodeCodabar_AddsStartStopAndRoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Codabar, Data = "12345" };
        var matrix = LinearEncoders.EncodeCodabar(spec);
        Assert.Equal("A12345A", Decode(matrix, BarcodeFormat.CODABAR));
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter LinearEncodersTests`
Expected: FAIL — `LinearEncoders` doesn't exist yet. (MSI is intentionally
untested here for round-trip decode — ZXing.Net's `MultiFormatOneDReader`
does not include an MSI decoder; `EncodeMsi`'s check-digit behavior is
already covered by `ChecksumsTests`, and Task 5 wires it into
`BarcodeGenerator` where a dedicated `EncodeMsi` structural test is added.)

- [ ] **Step 3: Implement `LinearEncoders`**

```csharp
// dotnet/src/EspBarcode.Generator/Encoding/LinearEncoders.cs
using ZXing;
using ZXing.OneD;

namespace EspBarcode.Generator.Encoding;

internal static class LinearEncoders
{
    private static readonly Dictionary<EncodeHintType, object> NoMargin = new() { [EncodeHintType.MARGIN] = 0 };

    private static RawMatrix Encode(ZXing.Writer writer, string data, BarcodeFormat format)
    {
        var bits = writer.encode(data, format, 0, 0, NoMargin);
        var grid = new bool[bits.Height, bits.Width];
        for (var y = 0; y < bits.Height; y++)
            for (var x = 0; x < bits.Width; x++)
                grid[y, x] = bits[x, y];
        return RawMatrix.FromGrid(grid);
    }

    public static RawMatrix EncodeUpcA(BarcodeSpec spec) => Encode(new UPCAWriter(), Checksums.NormalizeUpcA(spec.Data, spec.Checksum), BarcodeFormat.UPC_A);
    public static RawMatrix EncodeEan13(BarcodeSpec spec) => Encode(new EAN13Writer(), Checksums.NormalizeEan13(spec.Data, spec.Checksum), BarcodeFormat.EAN_13);
    public static RawMatrix EncodeEan8(BarcodeSpec spec) => Encode(new EAN8Writer(), Checksums.NormalizeEan8(spec.Data, spec.Checksum), BarcodeFormat.EAN_8);
    public static RawMatrix EncodeItf(BarcodeSpec spec) => Encode(new ITFWriter(), Checksums.NormalizeItf(spec.Data), BarcodeFormat.ITF);
    public static RawMatrix EncodeItf14(BarcodeSpec spec) => Encode(new ITFWriter(), Checksums.NormalizeItf14(spec.Data, spec.Checksum), BarcodeFormat.ITF);
    public static RawMatrix EncodeMsi(BarcodeSpec spec) => Encode(new MSIWriter(), Checksums.NormalizeMsi(spec.Data, spec.Checksum), BarcodeFormat.MSI);
    public static RawMatrix EncodeCode39(BarcodeSpec spec) => Encode(new Code39Writer(), spec.Data.ToUpperInvariant(), BarcodeFormat.CODE_39);
    public static RawMatrix EncodeCodabar(BarcodeSpec spec) => Encode(new CodaBarWriter(), Checksums.NormalizeCodabar(spec.Data), BarcodeFormat.CODABAR);
}
```

If `ZXing.OneD.MSIWriter` is not the exact namespace/class name found at
build time, check `ZXing.OneD` (confirmed present in ZXing.Net 0.16.11
source as `Source/lib/oned/MSIWriter.cs`) and adjust the `using`
accordingly — do not substitute a different symbology.

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter LinearEncodersTests`
Expected: PASS. If `EncodeCode39` fails because `Code39Writer` expects
pre-added start/stop `*` characters (behavior varies by ZXing.Net
version), adjust `EncodeCode39` to strip/add `*` as needed so the round
trip passes and the public behavior still matches the ESP's documented
"start/stop added automatically" — do not weaken the test's assertion.

- [ ] **Step 5: Commit**

```bash
git add dotnet/
git commit -m "Add linear/checksum-family barcode encoders (UPC-A, EAN-13/8, ITF, ITF-14, MSI, Code 39, Codabar)"
```

---

### Task 5: Code 128/GS1-128 + matrix-family encoders, and the `BarcodeGenerator` dispatcher

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/Encoding/Code128Encoders.cs`
- Create: `dotnet/src/EspBarcode.Generator/Encoding/MatrixEncoders.cs`
- Create: `dotnet/src/EspBarcode.Generator/BarcodeGenerator.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/Code128EncodersTests.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/MatrixEncodersTests.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/BarcodeGeneratorTests.cs`

**Interfaces:**
- Consumes: `LinearEncoders` (Task 4), `Checksums` (Task 3), `BarcodeSpec`/`BarcodeType` (Task 2).
- Produces:
  - `internal static class Code128Encoders`: `EncodeCode128(BarcodeSpec) -> RawMatrix`, `EncodeGs1_128(BarcodeSpec) -> RawMatrix`.
  - `internal static class MatrixEncoders`: `EncodeQr`, `EncodeDataMatrix`, `EncodeAztec`, `EncodePdf417`, each `(BarcodeSpec) -> RawMatrix`.
  - `public static class BarcodeGenerator` with
    `Encode(BarcodeSpec spec) -> RawMatrix` — the single public entry
    point every later task (layout, CLI, GUI) calls. Dispatches on
    `spec.Type`; throws `BarcodeGenerationException("aztec_rune_unsupported", ...)`
    when `spec.Type == BarcodeType.Aztec && spec.AztecLayers == 0`.

- [ ] **Step 1: Write the failing tests**

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/Code128EncodersTests.cs
using EspBarcode.Generator.Encoding;
using ZXing;
using ZXing.OneD;

namespace EspBarcode.Generator.Tests;

public class Code128EncodersTests
{
    private static string Decode(RawMatrix matrix)
    {
        var bits = new ZXing.Common.BitMatrix(matrix.Width, matrix.Height);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                if (matrix[x, y]) bits[x, y] = true;
        var reader = new Code128Reader();
        var luminance = new RGBLuminanceSourceFromBitMatrix(bits);
        var binaryBitmap = new BinaryBitmap(new ZXing.Common.HybridBinarizer(luminance));
        var result = reader.decode(binaryBitmap);
        Assert.NotNull(result);
        return result!.Text;
    }

    [Fact]
    public void EncodeCode128_PlainText_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Code128, Data = "LOT-2026-00042" };
        Assert.Equal("LOT-2026-00042", Decode(Code128Encoders.EncodeCode128(spec)));
    }

    [Fact]
    public void EncodeGs1_128_PrependsFnc1AndRoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Gs1_128, Data = "10ABC" };
        var text = Decode(Code128Encoders.EncodeGs1_128(spec));
        Assert.Contains("10ABC", text);
    }
}
```

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/MatrixEncodersTests.cs
using EspBarcode.Generator.Encoding;
using ZXing;
using ZXing.Datamatrix;
using ZXing.QrCode;
using ZXing.Aztec;
using ZXing.PDF417;

namespace EspBarcode.Generator.Tests;

public class MatrixEncodersTests
{
    [Fact]
    public void EncodeQr_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };
        var matrix = MatrixEncoders.EncodeQr(spec);
        var reader = new QRCodeReader();
        var result = reader.decode(ToBinaryBitmap(matrix));
        Assert.Equal("LAB-TEST-001", result!.Text);
    }

    [Fact]
    public void EncodeDataMatrix_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.DataMatrix, Data = "DM-ROUNDTRIP-123" };
        var matrix = MatrixEncoders.EncodeDataMatrix(spec);
        var reader = new DataMatrixReader();
        var result = reader.decode(ToBinaryBitmap(matrix));
        Assert.Equal("DM-ROUNDTRIP-123", result!.Text);
    }

    [Fact]
    public void EncodeDataMatrix_Rectangular_ProducesWiderThanTallMatrix()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.DataMatrix, Data = "12345678", Rectangular = true };
        var matrix = MatrixEncoders.EncodeDataMatrix(spec);
        Assert.NotEqual(matrix.Width, matrix.Height);
    }

    [Fact]
    public void EncodeAztec_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Aztec, Data = "AZTEC-TEST" };
        var matrix = MatrixEncoders.EncodeAztec(spec);
        var reader = new AztecReader();
        var result = reader.decode(ToBinaryBitmap(matrix));
        Assert.Equal("AZTEC-TEST", result!.Text);
    }

    [Fact]
    public void EncodePdf417_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Pdf417, Data = "PDF417-TEST" };
        var matrix = MatrixEncoders.EncodePdf417(spec);
        var reader = new PDF417Reader();
        var result = reader.decode(ToBinaryBitmap(matrix));
        Assert.Equal("PDF417-TEST", result!.Text);
    }

    private static BinaryBitmap ToBinaryBitmap(RawMatrix matrix)
    {
        var bits = new ZXing.Common.BitMatrix(matrix.Width, matrix.Height);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                if (matrix[x, y]) bits[x, y] = true;
        return new BinaryBitmap(new ZXing.Common.HybridBinarizer(new RGBLuminanceSourceFromBitMatrix(bits)));
    }
}
```

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/BarcodeGeneratorTests.cs
namespace EspBarcode.Generator.Tests;

public class BarcodeGeneratorTests
{
    [Theory]
    [InlineData(BarcodeType.Qr, "LAB-TEST-001")]
    [InlineData(BarcodeType.Code128, "LOT-2026-00042")]
    [InlineData(BarcodeType.UpcA, "03600029145")]
    public void Encode_DispatchesWithoutThrowing(BarcodeType type, string data)
    {
        var matrix = BarcodeGenerator.Encode(new BarcodeSpec { Type = type, Data = data });
        Assert.True(matrix.Width > 0);
        Assert.True(matrix.Height > 0);
    }

    [Fact]
    public void Encode_AztecRune_ThrowsUnsupported()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Aztec, Data = "42", AztecLayers = 0 };
        var ex = Assert.Throws<BarcodeGenerationException>(() => BarcodeGenerator.Encode(spec));
        Assert.Equal("aztec_rune_unsupported", ex.Code);
    }
}
```

- [ ] **Step 2: Run to verify all three test files fail to compile/run**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter "Code128EncodersTests|MatrixEncodersTests|BarcodeGeneratorTests"`
Expected: FAIL — none of the production types exist yet.

If `RGBLuminanceSourceFromBitMatrix` isn't a real ZXing.Net helper type,
replace it in both test files with a small private helper class in each
test file that implements `ZXing.LuminanceSource` by returning black
(`0x00`) for `true` modules and white (`0xFF`) for `false`, e.g.:

```csharp
private sealed class BitMatrixLuminanceSource(ZXing.Common.BitMatrix matrix) : LuminanceSource(matrix.Width, matrix.Height)
{
    public override byte[] getRow(int y, byte[]? row)
    {
        row ??= new byte[Width];
        for (var x = 0; x < Width; x++) row[x] = matrix[x, y] ? (byte)0 : (byte)255;
        return row;
    }
    public override byte[] Matrix
    {
        get
        {
            var result = new byte[Width * Height];
            for (var y = 0; y < Height; y++)
                for (var x = 0; x < Width; x++)
                    result[y * Width + x] = matrix[x, y] ? (byte)0 : (byte)255;
            return result;
        }
    }
}
```

Use this in place of `RGBLuminanceSourceFromBitMatrix` in Task 4's and
this task's tests if the latter doesn't compile.

- [ ] **Step 3: Implement `Code128Encoders`**

```csharp
// dotnet/src/EspBarcode.Generator/Encoding/Code128Encoders.cs
using ZXing;
using ZXing.OneD;

namespace EspBarcode.Generator.Encoding;

internal static class Code128Encoders
{
    public static RawMatrix EncodeCode128(BarcodeSpec spec) => Encode(spec, gs1: false);
    public static RawMatrix EncodeGs1_128(BarcodeSpec spec) => Encode(spec, gs1: true);

    private static RawMatrix Encode(BarcodeSpec spec, bool gs1)
    {
        var data = Checksums.NormalizeFnc1Tokens(spec.Data);
        var hints = new Dictionary<EncodeHintType, object> { [EncodeHintType.MARGIN] = 0 };
        if (gs1) hints[EncodeHintType.GS1_FORMAT] = true;

        var writer = new Code128Writer();
        var bits = writer.encode(data, BarcodeFormat.CODE_128, 0, 0, hints);
        var grid = new bool[bits.Height, bits.Width];
        for (var y = 0; y < bits.Height; y++)
            for (var x = 0; x < bits.Width; x++)
                grid[y, x] = bits[x, y];
        return RawMatrix.FromGrid(grid);
    }
}
```

- [ ] **Step 4: Implement `MatrixEncoders`**

```csharp
// dotnet/src/EspBarcode.Generator/Encoding/MatrixEncoders.cs
using ZXing;
using ZXing.Aztec;
using ZXing.Datamatrix;
using ZXing.PDF417;
using ZXing.QrCode;
using ZXing.QrCode.Internal;

namespace EspBarcode.Generator.Encoding;

internal static class MatrixEncoders
{
    private static RawMatrix FromBitMatrix(ZXing.Common.BitMatrix bits)
    {
        var grid = new bool[bits.Height, bits.Width];
        for (var y = 0; y < bits.Height; y++)
            for (var x = 0; x < bits.Width; x++)
                grid[y, x] = bits[x, y];
        return RawMatrix.FromGrid(grid);
    }

    public static RawMatrix EncodeQr(BarcodeSpec spec)
    {
        var ecc = spec.Ecc.ToUpperInvariant() switch
        {
            "L" => ErrorCorrectionLevel.L,
            "M" => ErrorCorrectionLevel.M,
            "Q" => ErrorCorrectionLevel.Q,
            "H" => ErrorCorrectionLevel.H,
            _ => throw new BarcodeGenerationException("invalid_option", $"Unknown QR ecc level '{spec.Ecc}'."),
        };

        var writer = new QRCodeWriter();
        for (var version = Math.Max(1, spec.QrMinVersion); version <= Math.Min(40, spec.QrMaxVersion); version++)
        {
            var hints = new Dictionary<EncodeHintType, object>
            {
                [EncodeHintType.MARGIN] = 0,
                [EncodeHintType.ERROR_CORRECTION] = ecc,
                [EncodeHintType.QR_VERSION] = version,
            };
            try
            {
                return FromBitMatrix(writer.encode(spec.Data, BarcodeFormat.QR_CODE, 0, 0, hints));
            }
            catch (WriterException)
            {
                // Data doesn't fit this version at the requested ECC level; try the next one.
            }
        }
        throw new BarcodeGenerationException("data_too_long", $"Data does not fit any QR version in [{spec.QrMinVersion}, {spec.QrMaxVersion}] at ECC {spec.Ecc}.");
    }

    public static RawMatrix EncodeDataMatrix(BarcodeSpec spec)
    {
        var hints = new Dictionary<EncodeHintType, object>
        {
            [EncodeHintType.MARGIN] = 0,
            [EncodeHintType.DATA_MATRIX_SHAPE] = spec.Rectangular ? SymbolShapeHint.FORCE_RECTANGLE : SymbolShapeHint.FORCE_NONE,
        };
        var writer = new DataMatrixWriter();
        return FromBitMatrix(writer.encode(spec.Data, BarcodeFormat.DATA_MATRIX, 0, 0, hints));
    }

    public static RawMatrix EncodeAztec(BarcodeSpec spec)
    {
        var hints = new Dictionary<EncodeHintType, object>
        {
            [EncodeHintType.MARGIN] = 0,
            [EncodeHintType.ERROR_CORRECTION] = spec.AztecSecurity,
        };
        if (spec.AztecLayers != 1) hints[EncodeHintType.AZTEC_LAYERS] = spec.AztecLayers;

        var writer = new AztecWriter();
        return FromBitMatrix(writer.encode(spec.Data, BarcodeFormat.AZTEC, 0, 0, hints));
    }

    public static RawMatrix EncodePdf417(BarcodeSpec spec)
    {
        var hints = new Dictionary<EncodeHintType, object> { [EncodeHintType.MARGIN] = 0 };
        var writer = new PDF417Writer();
        return FromBitMatrix(writer.encode(spec.Data, BarcodeFormat.PDF_417, 0, 0, hints));
    }
}
```

If `ZXing.QrCode.Internal.ErrorCorrectionLevel`/`SymbolShapeHint` resolve
to different namespaces in 0.16.11 (namespaces occasionally shift between
ZXing.Net versions), use `dotnet build` errors to locate the correct
`using` — do not change the hint keys/enum value names themselves, those
were confirmed against source.

- [ ] **Step 5: Implement `BarcodeGenerator`**

```csharp
// dotnet/src/EspBarcode.Generator/BarcodeGenerator.cs
using EspBarcode.Generator.Encoding;

namespace EspBarcode.Generator;

/// <summary>Single public entry point for barcode generation — the "core package" referenced by both EspBarcode.Viewer.Cli and EspBarcode.Viewer.Gui.</summary>
public static class BarcodeGenerator
{
    public static RawMatrix Encode(BarcodeSpec spec)
    {
        if (spec.Type == BarcodeType.Aztec && spec.AztecLayers == 0)
        {
            throw new BarcodeGenerationException(
                "aztec_rune_unsupported",
                "Aztec Rune (aztec_layers=0) is not supported by the standalone generator; use a positive aztec_layers value.");
        }

        return spec.Type switch
        {
            BarcodeType.Qr => MatrixEncoders.EncodeQr(spec),
            BarcodeType.DataMatrix => MatrixEncoders.EncodeDataMatrix(spec),
            BarcodeType.Aztec => MatrixEncoders.EncodeAztec(spec),
            BarcodeType.Pdf417 => MatrixEncoders.EncodePdf417(spec),
            BarcodeType.Code128 => Code128Encoders.EncodeCode128(spec),
            BarcodeType.Gs1_128 => Code128Encoders.EncodeGs1_128(spec),
            BarcodeType.Code39 => LinearEncoders.EncodeCode39(spec),
            BarcodeType.UpcA => LinearEncoders.EncodeUpcA(spec),
            BarcodeType.Ean13 => LinearEncoders.EncodeEan13(spec),
            BarcodeType.Ean8 => LinearEncoders.EncodeEan8(spec),
            BarcodeType.Itf => LinearEncoders.EncodeItf(spec),
            BarcodeType.Itf14 => LinearEncoders.EncodeItf14(spec),
            BarcodeType.Codabar => LinearEncoders.EncodeCodabar(spec),
            BarcodeType.Msi => LinearEncoders.EncodeMsi(spec),
            _ => throw new ArgumentOutOfRangeException(nameof(spec), spec.Type, "Unknown barcode type"),
        };
    }
}
```

- [ ] **Step 6: Run to verify everything passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests`
Expected: PASS — every test file from Tasks 1-5.

- [ ] **Step 7: Commit**

```bash
git add dotnet/
git commit -m "Add Code128/GS1-128 and matrix-family encoders, wire up BarcodeGenerator dispatcher"
```

---

### Task 6: `ScreenFitLayout` (integer module scale + auto-rotation)

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/ScreenFitLayout.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/ScreenFitLayoutTests.cs`

**Interfaces:**
- Consumes: `RawMatrix` (Task 1), `BarcodeGenerationException` (Task 2).
- Produces:
  - `public sealed record RenderedLayout(RawMatrix Matrix, int Scale, int QuietModules, int OffsetXPixels, int OffsetYPixels, int CanvasWidth, int CanvasHeight)` — `Matrix` here is already the possibly-90-degree-rotated symbol; quiet zone is expressed in modules and applied by the renderer (Task 8), not baked into `Matrix`.
  - `public static class ScreenFitLayout` with
    `Fit(RawMatrix matrix, int quiet, int minModule, string rotation, int canvasWidth, int canvasHeight) -> RenderedLayout`.
    Throws `BarcodeGenerationException("too_dense", ...)` when no integer
    scale `>= minModule` fits either orientation.
    `quiet < 0` (the spec's "-1 = symbology default") must be resolved by
    the caller before calling `Fit` — `Fit` itself always takes a
    resolved non-negative `quiet` value.
  - `public static int ResolveQuietZone(BarcodeType type, int specQuiet)` —
    returns `specQuiet` unchanged if `>= 0`; otherwise `4` for
    `Qr/DataMatrix/Aztec/Pdf417`, `10` for every other (linear) type.

- [ ] **Step 1: Write the failing tests**

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/ScreenFitLayoutTests.cs
namespace EspBarcode.Generator.Tests;

public class ScreenFitLayoutTests
{
    private static RawMatrix SquareMatrix(int size)
    {
        var m = new RawMatrix(size, size);
        for (var y = 0; y < size; y++)
            for (var x = 0; x < size; x++)
                m[x, y] = (x + y) % 2 == 0;
        return m;
    }

    [Fact]
    public void ResolveQuietZone_ExplicitValue_ReturnedUnchanged()
    {
        Assert.Equal(7, ScreenFitLayout.ResolveQuietZone(BarcodeType.Qr, 7));
    }

    [Fact]
    public void ResolveQuietZone_MatrixDefault_IsFour()
    {
        Assert.Equal(4, ScreenFitLayout.ResolveQuietZone(BarcodeType.DataMatrix, -1));
    }

    [Fact]
    public void ResolveQuietZone_LinearDefault_IsTen()
    {
        Assert.Equal(10, ScreenFitLayout.ResolveQuietZone(BarcodeType.Code128, -1));
    }

    [Fact]
    public void Fit_SquareMatrixInSquareCanvas_PicksLargestIntegerScale()
    {
        // 21x21 symbol + 4-module quiet zone each side = 29 logical modules.
        // 480 / 29 = 16.5..., so the largest integer scale is 16.
        var layout = ScreenFitLayout.Fit(SquareMatrix(21), quiet: 4, minModule: 2, rotation: "0", canvasWidth: 480, canvasHeight: 480);
        Assert.Equal(16, layout.Scale);
    }

    [Fact]
    public void Fit_Auto_PicksBetterFittingOrientationForWideMatrix()
    {
        // 100-wide x 10-tall linear symbol in a 320x480 portrait canvas:
        // unrotated needs width 320/(100+20)=2 scale; rotated (10x100 effective footprint on its side)
        // fits height-wise as 480/(100+20)=4, width as 320/(10+20)=10 -> min 4. Rotated wins.
        var matrix = new RawMatrix(100, 10);
        var layout = ScreenFitLayout.Fit(matrix, quiet: 10, minModule: 1, rotation: "auto", canvasWidth: 320, canvasHeight: 480);
        Assert.Equal(100, layout.Matrix.Height); // rotated: original width becomes height
        Assert.Equal(10, layout.Matrix.Width);
    }

    [Fact]
    public void Fit_TooDenseForCanvas_ThrowsTooDense()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            ScreenFitLayout.Fit(SquareMatrix(500), quiet: 4, minModule: 8, rotation: "0", canvasWidth: 320, canvasHeight: 480));
        Assert.Equal("too_dense", ex.Code);
    }

    [Fact]
    public void Fit_ExplicitRotation0_NeverRotates()
    {
        var matrix = new RawMatrix(100, 10);
        var layout = ScreenFitLayout.Fit(matrix, quiet: 10, minModule: 1, rotation: "0", canvasWidth: 320, canvasHeight: 480);
        Assert.Equal(100, layout.Matrix.Width);
        Assert.Equal(10, layout.Matrix.Height);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter ScreenFitLayoutTests`
Expected: FAIL — `ScreenFitLayout` doesn't exist yet.

- [ ] **Step 3: Implement `ScreenFitLayout`**

```csharp
// dotnet/src/EspBarcode.Generator/ScreenFitLayout.cs
namespace EspBarcode.Generator;

/// <summary>A matrix laid out for a specific canvas: possibly rotated, with its integer pixel scale and centering offsets. Nothing here is resized/antialiased — see docs/ARCHITECTURE.md "Rendering pipeline", which this reimplements against an arbitrary target canvas instead of the ESP's fixed 320x480 screen.</summary>
public sealed record RenderedLayout(RawMatrix Matrix, int Scale, int QuietModules, int OffsetXPixels, int OffsetYPixels, int CanvasWidth, int CanvasHeight);

public static class ScreenFitLayout
{
    public static int ResolveQuietZone(BarcodeType type, int specQuiet)
    {
        if (specQuiet >= 0) return specQuiet;
        return type is BarcodeType.Qr or BarcodeType.DataMatrix or BarcodeType.Aztec or BarcodeType.Pdf417 ? 4 : 10;
    }

    public static RenderedLayout Fit(RawMatrix matrix, int quiet, int minModule, string rotation, int canvasWidth, int canvasHeight)
    {
        var scale0 = IntegerScale(matrix.Width, matrix.Height, quiet, canvasWidth, canvasHeight);
        var scale90 = IntegerScale(matrix.Height, matrix.Width, quiet, canvasWidth, canvasHeight);

        bool rotate;
        int scale;
        switch (rotation)
        {
            case "0" or "180":
                rotate = false;
                scale = scale0;
                break;
            case "90" or "270":
                rotate = true;
                scale = scale90;
                break;
            case "auto":
                rotate = scale90 > scale0;
                scale = rotate ? scale90 : scale0;
                break;
            default:
                throw new BarcodeGenerationException("invalid_option", $"Unknown rotation '{rotation}'.");
        }

        if (scale < minModule)
        {
            throw new BarcodeGenerationException(
                "too_dense",
                $"Symbol ({matrix.Width}x{matrix.Height} modules, quiet {quiet}) does not fit a {canvasWidth}x{canvasHeight} canvas at min_module {minModule}.");
        }

        var laidOutMatrix = rotate ? Rotate90(matrix) : matrix;
        var logicalWidth = laidOutMatrix.Width + 2 * quiet;
        var logicalHeight = laidOutMatrix.Height + 2 * quiet;
        var offsetX = (canvasWidth - logicalWidth * scale) / 2;
        var offsetY = (canvasHeight - logicalHeight * scale) / 2;

        return new RenderedLayout(laidOutMatrix, scale, quiet, offsetX, offsetY, canvasWidth, canvasHeight);
    }

    private static int IntegerScale(int width, int height, int quiet, int canvasWidth, int canvasHeight)
    {
        var logicalWidth = width + 2 * quiet;
        var logicalHeight = height + 2 * quiet;
        return Math.Min(canvasWidth / logicalWidth, canvasHeight / logicalHeight);
    }

    private static RawMatrix Rotate90(RawMatrix matrix)
    {
        var rotated = new RawMatrix(matrix.Height, matrix.Width);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                rotated[y, matrix.Width - 1 - x] = matrix[x, y];
        return rotated;
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter ScreenFitLayoutTests`
Expected: PASS. If `Fit_Auto_PicksBetterFittingOrientationForWideMatrix`'s
exact numbers don't match (arithmetic above was worked by hand), trust
the test's *intent* (rotated orientation wins for this wide-and-short
matrix in a portrait canvas) over the exact literal scale value — recompute
by hand from `IntegerScale`'s formula and fix the test's asserted numbers
to match, not the production logic.

- [ ] **Step 5: Commit**

```bash
git add dotnet/
git commit -m "Add ScreenFitLayout: integer module scale + auto-rotation against an arbitrary target canvas"
```

---

### Task 7: `PayloadSource` (`@file` payload resolution)

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/PayloadSource.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/PayloadSourceTests.cs`

**Interfaces:**
- Consumes: nothing.
- Produces: `public static class PayloadSource` with
  `Resolve(string argument) -> string` — if `argument` starts with `@`,
  reads the remainder as a UTF-8 file path and returns its full text
  (trimmed of a single trailing newline, matching typical `@file`
  conventions and `tools/espbarcode.py`); otherwise returns `argument`
  unchanged. Throws `BarcodeGenerationException("payload_file_not_found", ...)`
  if the file doesn't exist.

- [ ] **Step 1: Write the failing tests**

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/PayloadSourceTests.cs
namespace EspBarcode.Generator.Tests;

public class PayloadSourceTests
{
    [Fact]
    public void Resolve_LiteralArgument_ReturnedUnchanged()
    {
        Assert.Equal("LAB-TEST-001", PayloadSource.Resolve("LAB-TEST-001"));
    }

    [Fact]
    public void Resolve_AtPrefixedArgument_ReadsFileContents()
    {
        var path = Path.GetTempFileName();
        try
        {
            File.WriteAllText(path, "FROM-FILE-PAYLOAD\n");
            Assert.Equal("FROM-FILE-PAYLOAD", PayloadSource.Resolve("@" + path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Resolve_AtPrefixedMissingFile_ThrowsPayloadFileNotFound()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => PayloadSource.Resolve("@" + Path.Combine(Path.GetTempPath(), "does-not-exist-" + Guid.NewGuid() + ".txt")));
        Assert.Equal("payload_file_not_found", ex.Code);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter PayloadSourceTests`
Expected: FAIL — `PayloadSource` doesn't exist yet.

- [ ] **Step 3: Implement `PayloadSource`**

```csharp
// dotnet/src/EspBarcode.Generator/PayloadSource.cs
namespace EspBarcode.Generator;

public static class PayloadSource
{
    public static string Resolve(string argument)
    {
        if (!argument.StartsWith('@')) return argument;

        var path = argument[1..];
        if (!File.Exists(path))
            throw new BarcodeGenerationException("payload_file_not_found", $"Payload file not found: '{path}'.");

        return File.ReadAllText(path).TrimEnd('\r', '\n');
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter PayloadSourceTests`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add dotnet/
git commit -m "Add PayloadSource: literal or @file payload resolution"
```

---

### Task 8: `BarcodeImageRenderer` (PNG output)

**Files:**
- Create: `dotnet/src/EspBarcode.Generator/BarcodeImageRenderer.cs`
- Test: `dotnet/tests/EspBarcode.Generator.Tests/BarcodeImageRendererTests.cs`

**Interfaces:**
- Consumes: `RenderedLayout` (Task 6).
- Produces: `public static class BarcodeImageRenderer` with
  `Render(RenderedLayout layout, bool invert) -> byte[]` (PNG-encoded).
  Background is white (or black if `invert`); each `true` module is drawn
  as an exact `Scale x Scale` black (or white if `invert`) rectangle at
  its integer pixel position — no antialiasing, no resizing.

- [ ] **Step 1: Write the failing test**

```csharp
// dotnet/tests/EspBarcode.Generator.Tests/BarcodeImageRendererTests.cs
using System.Drawing;
using System.Drawing.Imaging;

namespace EspBarcode.Generator.Tests;

public class BarcodeImageRendererTests
{
    [Fact]
    public void Render_ProducesPngOfExpectedCanvasSize()
    {
        var matrix = new RawMatrix(2, 2);
        matrix[0, 0] = true;
        matrix[1, 1] = true;
        var layout = new RenderedLayout(matrix, Scale: 10, QuietModules: 1, OffsetXPixels: 0, OffsetYPixels: 0, CanvasWidth: 100, CanvasHeight: 100);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        Assert.Equal(ImageFormat.Png.Guid, bitmap.RawFormat.Guid);
        Assert.Equal(100, bitmap.Width);
        Assert.Equal(100, bitmap.Height);
    }

    [Fact]
    public void Render_DrawnModulePixel_IsBlackOnWhiteByDefault()
    {
        var matrix = new RawMatrix(1, 1);
        matrix[0, 0] = true;
        var layout = new RenderedLayout(matrix, Scale: 10, QuietModules: 0, OffsetXPixels: 0, OffsetYPixels: 0, CanvasWidth: 10, CanvasHeight: 10);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        var center = bitmap.GetPixel(5, 5);
        var corner = bitmap.GetPixel(0, 0); // outside the 1x1 module at scale 10 in a 10x10 canvas -> actually covers whole canvas; assert black
        Assert.Equal(Color.FromArgb(255, 0, 0, 0).ToArgb(), center.ToArgb());
    }

    [Fact]
    public void Render_Invert_SwapsBackgroundAndModuleColors()
    {
        var matrix = new RawMatrix(1, 1);
        matrix[0, 0] = false; // unset module -> shows background color
        var layout = new RenderedLayout(matrix, Scale: 10, QuietModules: 0, OffsetXPixels: 0, OffsetYPixels: 0, CanvasWidth: 10, CanvasHeight: 10);

        var png = BarcodeImageRenderer.Render(layout, invert: true);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        var pixel = bitmap.GetPixel(5, 5);
        Assert.Equal(Color.FromArgb(255, 0, 0, 0).ToArgb(), pixel.ToArgb()); // inverted background is black
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter BarcodeImageRendererTests`
Expected: FAIL — `BarcodeImageRenderer` doesn't exist yet.

- [ ] **Step 3: Implement `BarcodeImageRenderer`**

```csharp
// dotnet/src/EspBarcode.Generator/BarcodeImageRenderer.cs
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.Versioning;

namespace EspBarcode.Generator;

[SupportedOSPlatform("windows")]
public static class BarcodeImageRenderer
{
    public static byte[] Render(RenderedLayout layout, bool invert)
    {
        var background = invert ? Color.Black : Color.White;
        var foreground = invert ? Color.White : Color.Black;

        using var bitmap = new Bitmap(layout.CanvasWidth, layout.CanvasHeight, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(bitmap))
        {
            g.Clear(background);
            using var brush = new SolidBrush(foreground);
            for (var y = 0; y < layout.Matrix.Height; y++)
            {
                for (var x = 0; x < layout.Matrix.Width; x++)
                {
                    if (!layout.Matrix[x, y]) continue;
                    var px = layout.OffsetXPixels + (layout.QuietModules + x) * layout.Scale;
                    var py = layout.OffsetYPixels + (layout.QuietModules + y) * layout.Scale;
                    g.FillRectangle(brush, px, py, layout.Scale, layout.Scale);
                }
            }
        }

        using var output = new MemoryStream();
        bitmap.Save(output, ImageFormat.Png);
        return output.ToArray();
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Generator.Tests --filter BarcodeImageRendererTests`
Expected: PASS. This test project runs on `windows-latest` in CI (Task
13), so `System.Drawing.Common` works; if run locally on non-Windows for
development, these three tests are expected to fail/be skipped —
acceptable per the spec's CI-runner decision.

- [ ] **Step 5: Commit**

```bash
git add dotnet/
git commit -m "Add BarcodeImageRenderer: pixel-exact PNG rendering from a RenderedLayout"
```

---

### Task 9: `EspBarcode.Viewer.Cli` — `generate` command (file output + system-viewer open)

**Files:**
- Create: `dotnet/src/EspBarcode.Viewer.Cli/EspBarcode.Viewer.Cli.csproj`
- Create: `dotnet/src/EspBarcode.Viewer.Cli/AssemblyInfo.cs`
- Create: `dotnet/src/EspBarcode.Viewer.Cli/Program.cs`
- Create: `dotnet/src/EspBarcode.Viewer.Cli/SpecArgs.cs`
- Create: `dotnet/src/EspBarcode.Viewer.Cli/CliApp.cs`
- Create: `dotnet/tests/EspBarcode.Viewer.Cli.Tests/EspBarcode.Viewer.Cli.Tests.csproj`
- Create: `dotnet/tests/EspBarcode.Viewer.Cli.Tests/SpecArgsTests.cs`
- Modify: `dotnet/EspScreenBarcodeGenerator.slnx` (add both new projects)

**Interfaces:**
- Consumes: `BarcodeGenerator.Encode` (Task 5), `ScreenFitLayout.Fit`/`ResolveQuietZone` (Task 6), `PayloadSource.Resolve` (Task 7), `BarcodeImageRenderer.Render` (Task 8).
- Produces:
  - `internal static class SpecArgs` with
    `Parse(string[] args) -> ParsedGenerateCommand` where
    `ParsedGenerateCommand` is a record:
    `(BarcodeSpec Spec, string? OutPath, OpenMode Open)` and
    `enum OpenMode { None, System, Viewer }`. Throws
    `BarcodeGenerationException("invalid_args", ...)` on bad input (used
    by `CliApp` to print a clean error, not a stack trace).
  - `CliApp.Run(string[] args) -> int` (exit code) — this task wires only
    the `generate` command's `none`/`system` open modes end-to-end;
    `viewer` mode is wired in Task 10.

- [ ] **Step 1: Write the failing `SpecArgs` tests**

```csharp
// dotnet/tests/EspBarcode.Viewer.Cli.Tests/SpecArgsTests.cs
using EspBarcode.Generator;
using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class SpecArgsTests
{
    [Fact]
    public void Parse_MinimalGenerateCommand_UsesDefaults()
    {
        var parsed = SpecArgs.Parse(["generate", "qr", "LAB-TEST-001"]);
        Assert.Equal(BarcodeType.Qr, parsed.Spec.Type);
        Assert.Equal("LAB-TEST-001", parsed.Spec.Data);
        Assert.Null(parsed.OutPath);
        Assert.Equal(OpenMode.None, parsed.Open);
    }

    [Fact]
    public void Parse_AllOptions_MapsToSpecFields()
    {
        var parsed = SpecArgs.Parse([
            "generate", "datamatrix", "DM-TEST",
            "--out", "out.png",
            "--open", "system",
            "--ecc", "H",
            "--rotation", "90",
            "--quiet", "6",
            "--min-module", "3",
            "--rect",
            "--invert",
            "--no-checksum",
            "--qr-min-version", "2",
            "--qr-max-version", "10",
            "--aztec-security", "30",
            "--aztec-layers", "4",
        ]);

        Assert.Equal(BarcodeType.DataMatrix, parsed.Spec.Type);
        Assert.Equal("out.png", parsed.OutPath);
        Assert.Equal(OpenMode.System, parsed.Open);
        Assert.Equal("H", parsed.Spec.Ecc);
        Assert.Equal("90", parsed.Spec.Rotation);
        Assert.Equal(6, parsed.Spec.Quiet);
        Assert.Equal(3, parsed.Spec.MinModule);
        Assert.True(parsed.Spec.Rectangular);
        Assert.True(parsed.Spec.Invert);
        Assert.False(parsed.Spec.Checksum);
        Assert.Equal(2, parsed.Spec.QrMinVersion);
        Assert.Equal(10, parsed.Spec.QrMaxVersion);
        Assert.Equal(30, parsed.Spec.AztecSecurity);
        Assert.Equal(4, parsed.Spec.AztecLayers);
    }

    [Fact]
    public void Parse_OpenViewer_SetsOpenModeViewer()
    {
        var parsed = SpecArgs.Parse(["generate", "code128", "LOT-1", "--open", "viewer"]);
        Assert.Equal(OpenMode.Viewer, parsed.Open);
    }

    [Fact]
    public void Parse_UnknownType_ThrowsInvalidArgs()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => SpecArgs.Parse(["generate", "not-a-type", "DATA"]));
        Assert.Equal("invalid_args", ex.Code);
    }

    [Fact]
    public void Parse_MissingData_ThrowsInvalidArgs()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => SpecArgs.Parse(["generate", "qr"]));
        Assert.Equal("invalid_args", ex.Code);
    }
}
```

- [ ] **Step 2: Create the CLI and its test project, then run to verify the test fails**

```xml
<!-- dotnet/src/EspBarcode.Viewer.Cli/EspBarcode.Viewer.Cli.csproj -->
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <AssemblyName>espbarcode-viewer</AssemblyName>
    <RootNamespace>EspBarcode.Viewer.Cli</RootNamespace>
  </PropertyGroup>

  <ItemGroup>
    <ProjectReference Include="..\EspBarcode.Generator\EspBarcode.Generator.csproj" />
  </ItemGroup>

</Project>
```

```csharp
// dotnet/src/EspBarcode.Viewer.Cli/AssemblyInfo.cs
using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("EspBarcode.Viewer.Cli.Tests")]
```

```xml
<!-- dotnet/tests/EspBarcode.Viewer.Cli.Tests/EspBarcode.Viewer.Cli.Tests.csproj -->
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <IsPackable>false</IsPackable>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="coverlet.collector" Version="6.0.4" />
    <PackageReference Include="Microsoft.NET.Test.Sdk" Version="17.14.1" />
    <PackageReference Include="xunit" Version="2.9.3" />
    <PackageReference Include="xunit.runner.visualstudio" Version="3.1.4" />
  </ItemGroup>

  <ItemGroup>
    <Using Include="Xunit" />
  </ItemGroup>

  <ItemGroup>
    <ProjectReference Include="..\..\src\EspBarcode.Viewer.Cli\EspBarcode.Viewer.Cli.csproj" />
  </ItemGroup>

</Project>
```

Add both to `dotnet/EspScreenBarcodeGenerator.slnx` under `/src/` and
`/tests/` respectively (same pattern as Task 1 Step 7).

Run: `cd dotnet && dotnet test tests/EspBarcode.Viewer.Cli.Tests`
Expected: FAIL — `SpecArgs` doesn't exist yet.

- [ ] **Step 3: Implement `SpecArgs`**

```csharp
// dotnet/src/EspBarcode.Viewer.Cli/SpecArgs.cs
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

public enum OpenMode { None, System, Viewer }

public sealed record ParsedGenerateCommand(BarcodeSpec Spec, string? OutPath, OpenMode Open);

internal static class SpecArgs
{
    public static ParsedGenerateCommand Parse(string[] args)
    {
        if (args.Length < 2 || args[0] != "generate")
            throw new BarcodeGenerationException("invalid_args", "Usage: espbarcode-viewer generate <type> <data|@file> [options]");

        var type = BarcodeTypeExtensions.ParseWireValue(args[1]);
        if (args.Length < 3)
            throw new BarcodeGenerationException("invalid_args", "Missing <data|@file> argument.");

        var data = PayloadSource.Resolve(args[2]);
        var rest = args[3..];

        var outPath = ExtractOption(rest, "--out");
        var open = ExtractOption(rest, "--open") switch
        {
            null or "none" => OpenMode.None,
            "system" => OpenMode.System,
            "viewer" => OpenMode.Viewer,
            var other => throw new BarcodeGenerationException("invalid_args", $"Unknown --open mode '{other}'."),
        };

        var spec = new BarcodeSpec
        {
            Type = type,
            Data = data,
            Ecc = ExtractOption(rest, "--ecc") ?? "M",
            Rotation = ExtractOption(rest, "--rotation") ?? "auto",
            Quiet = int.Parse(ExtractOption(rest, "--quiet") ?? "-1"),
            MinModule = int.Parse(ExtractOption(rest, "--min-module") ?? "2"),
            Rectangular = HasFlag(rest, "--rect"),
            Invert = HasFlag(rest, "--invert"),
            Checksum = !HasFlag(rest, "--no-checksum"),
            QrMinVersion = int.Parse(ExtractOption(rest, "--qr-min-version") ?? "1"),
            QrMaxVersion = int.Parse(ExtractOption(rest, "--qr-max-version") ?? "20"),
            AztecSecurity = int.Parse(ExtractOption(rest, "--aztec-security") ?? "23"),
            AztecLayers = int.Parse(ExtractOption(rest, "--aztec-layers") ?? "1"),
        };

        return new ParsedGenerateCommand(spec, outPath, open);
    }

    private static string? ExtractOption(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == name) return args[i + 1];
        return null;
    }

    private static bool HasFlag(string[] args, string name) => args.Contains(name);
}
```

- [ ] **Step 4: Run to verify `SpecArgsTests` pass**

Run: `cd dotnet && dotnet test tests/EspBarcode.Viewer.Cli.Tests --filter SpecArgsTests`
Expected: PASS

- [ ] **Step 5: Implement `CliApp` and `Program` for `none`/`system` open modes (no dedicated unit test — this is process/file-system glue; verified by the manual smoke run in Task 13)**

```csharp
// dotnet/src/EspBarcode.Viewer.Cli/CliApp.cs
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

internal static class CliApp
{
    public static int Run(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
        {
            PrintUsage();
            return args.Length == 0 ? 1 : 0;
        }

        try
        {
            return args[0] switch
            {
                "generate" => RunGenerate(args),
                "close" => ViewerClient.Close(args[1..]),
                _ => Unknown(args[0]),
            };
        }
        catch (BarcodeGenerationException ex)
        {
            Console.Error.WriteLine($"error [{ex.Code}]: {ex.Message}");
            return 1;
        }
    }

    private static int RunGenerate(string[] args)
    {
        var parsed = SpecArgs.Parse(args);

        switch (parsed.Open)
        {
            case OpenMode.Viewer:
                ViewerClient.Render(parsed.Spec, args);
                if (parsed.OutPath is not null) WritePng(parsed.Spec, parsed.OutPath);
                break;

            case OpenMode.System:
            {
                var path = parsed.OutPath ?? Path.Combine(Path.GetTempPath(), $"espbarcode-{Guid.NewGuid():N}.png");
                WritePng(parsed.Spec, path);
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path) { UseShellExecute = true });
                Console.WriteLine(path);
                break;
            }

            case OpenMode.None:
            default:
                if (parsed.OutPath is not null) WritePng(parsed.Spec, parsed.OutPath);
                break;
        }

        return 0;
    }

    private static void WritePng(BarcodeSpec spec, string path)
    {
        var matrix = BarcodeGenerator.Encode(spec);
        var quiet = ScreenFitLayout.ResolveQuietZone(spec.Type, spec.Quiet);
        var layout = ScreenFitLayout.Fit(matrix, quiet, spec.MinModule, spec.Rotation, canvasWidth: 320, canvasHeight: 480);
        var png = BarcodeImageRenderer.Render(layout, spec.Invert);
        File.WriteAllBytes(path, png);
    }

    private static int Unknown(string command)
    {
        Console.Error.WriteLine($"error: unknown command '{command}'");
        PrintUsage();
        return 1;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            espbarcode-viewer - standalone barcode generator/viewer (no ESP required)

            Usage:
              espbarcode-viewer generate <type> <data|@file> [options]
              espbarcode-viewer close [--viewer-port N]

            Options:
              --out PATH               Write a PNG to PATH
              --open none|system|viewer  Open mode (default: none)
              --ecc L|M|Q|H             QR error correction (default: M)
              --rotation auto|0|90|180|270
              --quiet N                 Quiet zone in modules, -1 = symbology default
              --min-module N            Minimum pixels per module (default: 2)
              --rect                    Request rectangular Data Matrix
              --invert                  White modules on black background
              --no-checksum             Disable MSI/retail check-digit computation
              --qr-min-version N / --qr-max-version N
              --aztec-security N / --aztec-layers N
            """);
    }
}
```

```csharp
// dotnet/src/EspBarcode.Viewer.Cli/Program.cs
using EspBarcode.Viewer.Cli;

return CliApp.Run(args);
```

`ViewerClient.Render`/`ViewerClient.Close` are stubbed as
`throw new NotImplementedException()` for now — Task 10 implements them.
Add that stub class now so this task compiles:

```csharp
// dotnet/src/EspBarcode.Viewer.Cli/ViewerClient.cs (stub — replaced in Task 10)
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

internal static class ViewerClient
{
    public static void Render(BarcodeSpec spec, string[] args) => throw new NotImplementedException("Implemented in Task 10.");
    public static int Close(string[] args) => throw new NotImplementedException("Implemented in Task 10.");
}
```

- [ ] **Step 6: Build and run the full test suite**

Run: `cd dotnet && dotnet build EspScreenBarcodeGenerator.slnx && dotnet test EspScreenBarcodeGenerator.slnx`
Expected: builds clean, all tests pass (viewer-mode paths aren't exercised
by tests yet — that's Task 10).

- [ ] **Step 7: Commit**

```bash
git add dotnet/
git commit -m "Add EspBarcode.Viewer.Cli generate command (file output + system-viewer open)"
```

---

### Task 10: `EspBarcode.Viewer.Cli` — viewer HTTP client (health probe, launch, render, close)

**Files:**
- Modify: `dotnet/src/EspBarcode.Viewer.Cli/ViewerClient.cs` (replace Task 9's stub)
- Modify: `dotnet/src/EspBarcode.Viewer.Cli/CliApp.cs` (pass an `HttpMessageHandler` seam through for testability — see Step 3)
- Create: `dotnet/tests/EspBarcode.Viewer.Cli.Tests/ViewerClientTests.cs`

**Interfaces:**
- Consumes: `ViewerProtocol` (Task 2), `BarcodeSpec` (Task 2).
- Produces: `internal static class ViewerClient` with:
  - `IsHealthy(HttpMessageHandler handler, int port) -> bool` — `GET
    /health` with a short timeout; `true` on any 2xx, `false` on any
    exception/non-2xx.
  - `PostRender(HttpMessageHandler handler, int port, BarcodeSpec spec) -> void` — `POST /render` with the spec as JSON; throws
    `BarcodeGenerationException("viewer_render_failed", ...)` on non-2xx.
  - `PostClose(HttpMessageHandler handler, int port) -> void` — `POST /close`.
  - `Render(BarcodeSpec spec, string[] args)` / `Close(string[] args)` —
    the real entry points `CliApp` calls: extract `--viewer-port`
    (default `ViewerProtocol.DefaultPort`) and `--viewer-exe`/
    `ESP_BARCODE_VIEWER_EXE_PATH` env var; probe health with a real
    `HttpClientHandler`; if unhealthy, locate and launch the GUI
    executable (next to the CLI's own `AppContext.BaseDirectory` by
    default), poll health up to a bounded number of retries, then POST.

- [ ] **Step 1: Write the failing tests against the testable, handler-injected methods**

```csharp
// dotnet/tests/EspBarcode.Viewer.Cli.Tests/ViewerClientTests.cs
using System.Net;
using EspBarcode.Generator;
using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class ViewerClientTests
{
    private sealed class StubHandler(HttpStatusCode status, Action<HttpRequestMessage>? onRequest = null) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            onRequest?.Invoke(request);
            return Task.FromResult(new HttpResponseMessage(status));
        }
    }

    [Fact]
    public void IsHealthy_TwoHundredResponse_ReturnsTrue()
    {
        Assert.True(ViewerClient.IsHealthy(new StubHandler(HttpStatusCode.OK), 47823));
    }

    [Fact]
    public void IsHealthy_ErrorResponse_ReturnsFalse()
    {
        Assert.False(ViewerClient.IsHealthy(new StubHandler(HttpStatusCode.ServiceUnavailable), 47823));
    }

    [Fact]
    public void IsHealthy_HandlerThrows_ReturnsFalse()
    {
        var handler = new ThrowingHandler();
        Assert.False(ViewerClient.IsHealthy(handler, 47823));
    }

    private sealed class ThrowingHandler : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
            => throw new HttpRequestException("connection refused");
    }

    [Fact]
    public void PostRender_TwoHundredResponse_DoesNotThrow()
    {
        HttpRequestMessage? captured = null;
        var handler = new StubHandler(HttpStatusCode.OK, r => captured = r);
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };

        ViewerClient.PostRender(handler, 47823, spec);

        Assert.NotNull(captured);
        Assert.Equal(HttpMethod.Post, captured!.Method);
        Assert.Equal($"/{ViewerProtocol.RenderPath.TrimStart('/')}", captured.RequestUri!.AbsolutePath);
    }

    [Fact]
    public void PostRender_ErrorResponse_ThrowsViewerRenderFailed()
    {
        var handler = new StubHandler(HttpStatusCode.BadRequest);
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };

        var ex = Assert.Throws<BarcodeGenerationException>(() => ViewerClient.PostRender(handler, 47823, spec));
        Assert.Equal("viewer_render_failed", ex.Code);
    }

    [Fact]
    public void PostClose_TwoHundredResponse_DoesNotThrow()
    {
        HttpRequestMessage? captured = null;
        var handler = new StubHandler(HttpStatusCode.OK, r => captured = r);

        ViewerClient.PostClose(handler, 47823);

        Assert.Equal(HttpMethod.Post, captured!.Method);
        Assert.Equal($"/{ViewerProtocol.ClosePath.TrimStart('/')}", captured.RequestUri!.AbsolutePath);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Viewer.Cli.Tests --filter ViewerClientTests`
Expected: FAIL — `ViewerClient` still has Task 9's `NotImplementedException` stub signatures, which don't match these new ones.

- [ ] **Step 3: Implement `ViewerClient`**

```csharp
// dotnet/src/EspBarcode.Viewer.Cli/ViewerClient.cs
using System.Net.Http.Json;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

internal static class ViewerClient
{
    public static bool IsHealthy(HttpMessageHandler handler, int port)
    {
        try
        {
            using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromMilliseconds(500) };
            var response = client.GetAsync($"http://127.0.0.1:{port}{ViewerProtocol.HealthPath}").GetAwaiter().GetResult();
            return response.IsSuccessStatusCode;
        }
        catch (Exception)
        {
            return false;
        }
    }

    public static void PostRender(HttpMessageHandler handler, int port, BarcodeSpec spec)
    {
        using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(5) };
        var response = client.PostAsJsonAsync($"http://127.0.0.1:{port}{ViewerProtocol.RenderPath}", spec).GetAwaiter().GetResult();
        if (!response.IsSuccessStatusCode)
            throw new BarcodeGenerationException("viewer_render_failed", $"Viewer returned {(int)response.StatusCode} for /render.");
    }

    public static void PostClose(HttpMessageHandler handler, int port)
    {
        using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(2) };
        client.PostAsync($"http://127.0.0.1:{port}{ViewerProtocol.ClosePath}", content: null).GetAwaiter().GetResult();
    }

    public static void Render(BarcodeSpec spec, string[] args)
    {
        var port = ExtractPort(args);
        using var handler = new HttpClientHandler();

        if (!IsHealthy(handler, port))
        {
            LaunchViewer(args, port);
            var attempts = 0;
            while (!IsHealthy(handler, port))
            {
                if (++attempts > 20) throw new BarcodeGenerationException("viewer_launch_failed", "Timed out waiting for EspBarcode.Viewer.Gui to become ready.");
                Thread.Sleep(250);
            }
        }

        PostRender(handler, port, spec);
    }

    public static int Close(string[] args)
    {
        var port = ExtractPort(args);
        using var handler = new HttpClientHandler();
        PostClose(handler, port);
        return 0;
    }

    private static int ExtractPort(string[] args)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == "--viewer-port") return int.Parse(args[i + 1]);
        return ViewerProtocol.DefaultPort;
    }

    private static void LaunchViewer(string[] args, int port)
    {
        string? exePath = null;
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == "--viewer-exe") exePath = args[i + 1];
        exePath ??= Environment.GetEnvironmentVariable("ESP_BARCODE_VIEWER_EXE_PATH");
        exePath ??= Path.Combine(AppContext.BaseDirectory, "EspBarcode.Viewer.Gui.exe");

        if (!File.Exists(exePath))
            throw new BarcodeGenerationException("viewer_exe_not_found", $"Viewer executable not found at '{exePath}'. Pass --viewer-exe or set ESP_BARCODE_VIEWER_EXE_PATH.");

        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(exePath, $"--port {port}") { UseShellExecute = false });
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Viewer.Cli.Tests`
Expected: PASS (all `ViewerClientTests`; `SpecArgsTests` from Task 9 still
pass too).

- [ ] **Step 5: Build the whole solution**

Run: `cd dotnet && dotnet build EspScreenBarcodeGenerator.slnx`
Expected: builds clean — `CliApp.RunGenerate`'s `ViewerClient.Render` call
now resolves against the real implementation.

- [ ] **Step 6: Commit**

```bash
git add dotnet/
git commit -m "Add EspBarcode.Viewer.Cli viewer HTTP client: health probe, auto-launch, render, close"
```

---

### Task 11: `EspBarcode.Viewer.Gui` — WPF window + embedded Kestrel host

**Files:**
- Create: `dotnet/src/EspBarcode.Viewer.Gui/EspBarcode.Viewer.Gui.csproj`
- Create: `dotnet/src/EspBarcode.Viewer.Gui/App.xaml`
- Create: `dotnet/src/EspBarcode.Viewer.Gui/App.xaml.cs`
- Create: `dotnet/src/EspBarcode.Viewer.Gui/MainWindow.xaml`
- Create: `dotnet/src/EspBarcode.Viewer.Gui/MainWindow.xaml.cs`
- Create: `dotnet/src/EspBarcode.Viewer.Gui/ViewerHost.cs`
- Modify: `dotnet/EspScreenBarcodeGenerator.slnx` (add the project — no test project, per the spec's "build-only in CI" decision)

**Interfaces:**
- Consumes: `BarcodeGenerator.Encode`, `ScreenFitLayout.Fit`/`ResolveQuietZone`, `BarcodeImageRenderer.Render` (all from `EspBarcode.Generator`), `ViewerProtocol` (Task 2).
- Produces: a runnable WPF application. No public API consumed by other
  projects — this is the leaf of the dependency graph.

- [ ] **Step 1: Create the project file**

```xml
<!-- dotnet/src/EspBarcode.Viewer.Gui/EspBarcode.Viewer.Gui.csproj -->
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <OutputType>WinExe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <UseWPF>true</UseWPF>
    <RootNamespace>EspBarcode.Viewer.Gui</RootNamespace>
  </PropertyGroup>

  <ItemGroup>
    <ProjectReference Include="..\EspBarcode.Generator\EspBarcode.Generator.csproj" />
  </ItemGroup>

  <ItemGroup>
    <PackageReference Include="Microsoft.AspNetCore.App.Ref" Version="10.0.0" />
  </ItemGroup>

  <ItemGroup>
    <FrameworkReference Include="Microsoft.AspNetCore.App" />
  </ItemGroup>

</Project>
```

Note: `net10.0-windows` overrides `Directory.Build.props`'s
`net10.0`/`ImplicitUsings`/`Nullable` — re-add those explicitly if the
override drops them:

```xml
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
```

If `Microsoft.AspNetCore.App.Ref` isn't a valid package reference for
this SDK combination (ASP.NET Core is normally consumed via
`<FrameworkReference Include="Microsoft.AspNetCore.App" />` alone in an
SDK-style project once `Microsoft.NET.Sdk.Web` or an explicit framework
reference is present) — drop the `Microsoft.AspNetCore.App.Ref` package
reference and keep only the `FrameworkReference`; that's the standard way
a non-Web-SDK project (like this WPF app) references Kestrel/minimal
APIs.

- [ ] **Step 2: `App.xaml` / `App.xaml.cs` — boots the WPF app and starts the HTTP host**

```xml
<!-- dotnet/src/EspBarcode.Viewer.Gui/App.xaml -->
<Application x:Class="EspBarcode.Viewer.Gui.App"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             StartupUri="MainWindow.xaml">
</Application>
```

```csharp
// dotnet/src/EspBarcode.Viewer.Gui/App.xaml.cs
using System.Windows;

namespace EspBarcode.Viewer.Gui;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var port = ParsePort(e.Args);
        var mainWindow = (MainWindow)MainWindow!;

        try
        {
            ViewerHost.Start(port, mainWindow);
        }
        catch (System.IO.IOException)
        {
            MessageBox.Show("A viewer is already running on this port.", "EspBarcode Viewer", MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
        }
    }

    private static int ParsePort(string[] args)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == "--port") return int.Parse(args[i + 1]);
        var env = Environment.GetEnvironmentVariable("ESP_BARCODE_VIEWER_PORT");
        return env is not null ? int.Parse(env) : EspBarcode.Generator.ViewerProtocol.DefaultPort;
    }
}
```

- [ ] **Step 3: `MainWindow.xaml` / `MainWindow.xaml.cs` — the display surface + live resize relayout**

```xml
<!-- dotnet/src/EspBarcode.Viewer.Gui/MainWindow.xaml -->
<Window x:Class="EspBarcode.Viewer.Gui.MainWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="EspBarcode Viewer" Width="480" Height="640"
        Background="White">
    <Grid Background="{Binding BackgroundBrush, RelativeSource={RelativeSource AncestorType=Window}}">
        <Image x:Name="BarcodeImage" Stretch="None" HorizontalAlignment="Center" VerticalAlignment="Center" />
    </Grid>
</Window>
```

```csharp
// dotnet/src/EspBarcode.Viewer.Gui/MainWindow.xaml.cs
using System.IO;
using System.Windows;
using System.Windows.Media.Imaging;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Gui;

public partial class MainWindow : Window
{
    private BarcodeSpec? _currentSpec;

    public MainWindow()
    {
        InitializeComponent();
        SizeChanged += (_, _) => RelayoutCurrentSpec();
    }

    public void ShowSpec(BarcodeSpec spec)
    {
        _currentSpec = spec;
        Dispatcher.Invoke(() =>
        {
            RelayoutCurrentSpec();
            Activate();
        });
    }

    private void RelayoutCurrentSpec()
    {
        if (_currentSpec is null) return;

        var spec = _currentSpec;
        var matrix = BarcodeGenerator.Encode(spec);
        var quiet = ScreenFitLayout.ResolveQuietZone(spec.Type, spec.Quiet);
        var canvasWidth = Math.Max(1, (int)ActualWidth);
        var canvasHeight = Math.Max(1, (int)ActualHeight);
        var layout = ScreenFitLayout.Fit(matrix, quiet, spec.MinModule, spec.Rotation, canvasWidth, canvasHeight);
        var png = BarcodeImageRenderer.Render(layout, spec.Invert);

        var bitmap = new BitmapImage();
        using (var stream = new MemoryStream(png))
        {
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.StreamSource = stream;
            bitmap.EndInit();
        }
        bitmap.Freeze();
        BarcodeImage.Source = bitmap;
    }
}
```

(Drop the `Background="{Binding BackgroundBrush, ...}"` binding from the
XAML if it's simpler to just set the `Grid`'s background directly from
code-behind in `RelayoutCurrentSpec` based on `spec.Invert` — either is
fine; the binding above is a placeholder for "background should track
invert" and can be replaced with a direct
`((Grid)Content).Background = spec.Invert ? Brushes.Black : Brushes.White;`
line in code-behind if the binding proves awkward in XAML.)

- [ ] **Step 4: `ViewerHost` — the Kestrel loopback server**

```csharp
// dotnet/src/EspBarcode.Viewer.Gui/ViewerHost.cs
using System.Net;
using Microsoft.AspNetCore.Builder;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Gui;

internal static class ViewerHost
{
    public static void Start(int port, MainWindow window)
    {
        var builder = WebApplication.CreateBuilder();
        builder.WebHost.ConfigureKestrel(options => options.Listen(IPAddress.Loopback, port));
        builder.Logging.ClearProviders();
        var app = builder.Build();

        app.MapGet(ViewerProtocol.HealthPath, () => Results.Ok());

        app.MapPost(ViewerProtocol.RenderPath, async (BarcodeSpec spec) =>
        {
            try
            {
                window.ShowSpec(spec);
                return Results.Ok();
            }
            catch (BarcodeGenerationException ex)
            {
                return Results.BadRequest(new { ex.Code, ex.Message });
            }
        });

        app.MapPost(ViewerProtocol.ClosePath, () =>
        {
            window.Dispatcher.Invoke(() => System.Windows.Application.Current.Shutdown());
            return Results.Ok();
        });

        _ = app.RunAsync();
    }
}
```

This binds the Kestrel listener eagerly during `Start` — if the port is
already in use, `options.Listen`/`app.RunAsync()` surfaces as a thrown
`IOException` (typically inside the `RunAsync` task rather than
synchronously from `Start` itself). Adjust `App.xaml.cs`'s `try/catch`
around `ViewerHost.Start` to also await the first moment Kestrel would
fail if `Start` returns before the listener is confirmed bound — the
simplest reliable option is to call `app.StartAsync().GetAwaiter().GetResult()`
instead of `_ = app.RunAsync()`, which blocks until the server is actually
listening (or throws) before `Start` returns, then let the app continue
running via WPF's own message loop (no need to await shutdown separately
since `Application.Current.Shutdown()` in `/close` exits the whole
process). Use `app.StartAsync()` for this reason.

- [ ] **Step 5: Wire the project into the solution**

Add to `dotnet/EspScreenBarcodeGenerator.slnx` under `/src/`:

```xml
    <Project Path="src/EspBarcode.Viewer.Gui/EspBarcode.Viewer.Gui.csproj" />
```

- [ ] **Step 6: Build (Windows-only — this step cannot run on a non-Windows dev machine)**

Run: `cd dotnet && dotnet build EspScreenBarcodeGenerator.slnx`
Expected: builds clean on Windows. If developing on non-Windows, skip
this step locally and rely on Task 13's CI run — do not mark this task
done until a Windows build has actually succeeded (locally or in CI).

- [ ] **Step 7: Manual smoke test (do this even if CI is the only Windows environment available — see Task 13's final smoke-test step, which repeats this against the built solution)**

Run: `dotnet run --project src/EspBarcode.Viewer.Gui -- --port 47823` in
one terminal, then in another:
`dotnet run --project src/EspBarcode.Viewer.Cli -- generate qr LAB-TEST-001 --open viewer --viewer-port 47823`.
Expected: the viewer window shows a QR code; resizing the window
re-lays-out the symbol; running the CLI again with a different type/data
updates the same window; `dotnet run --project src/EspBarcode.Viewer.Cli -- close --viewer-port 47823` closes it.

- [ ] **Step 8: Commit**

```bash
git add dotnet/
git commit -m "Add EspBarcode.Viewer.Gui: WPF viewer window with embedded Kestrel loopback server"
```

---

### Task 12: `EspBarcode.Viewer.Cli` `--viewer-exe` resolution fix-up and `list-types` command

**Files:**
- Modify: `dotnet/src/EspBarcode.Viewer.Cli/CliApp.cs` (add `list-types`)
- Modify: `dotnet/tests/EspBarcode.Viewer.Cli.Tests/SpecArgsTests.cs` is unaffected; add a new small test file instead.
- Create: `dotnet/tests/EspBarcode.Viewer.Cli.Tests/ListTypesTests.cs`

**Interfaces:**
- Consumes: `BarcodeTypeExtensions.ToWireValue` (Task 2).
- Produces: `internal static class CliApp` gains a `list-types` branch;
  extract the wire-value enumeration into a small testable method:
  `internal static IReadOnlyList<string> ListTypeWireValues()` returning
  all 14 `BarcodeType` wire values in enum declaration order.

- [ ] **Step 1: Write the failing test**

```csharp
// dotnet/tests/EspBarcode.Viewer.Cli.Tests/ListTypesTests.cs
using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class ListTypesTests
{
    [Fact]
    public void ListTypeWireValues_ReturnsAllFourteenTypes()
    {
        var values = CliApp.ListTypeWireValues();
        Assert.Equal(14, values.Count);
        Assert.Contains("qr", values);
        Assert.Contains("pdf417", values);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd dotnet && dotnet test tests/EspBarcode.Viewer.Cli.Tests --filter ListTypesTests`
Expected: FAIL — `CliApp.ListTypeWireValues` doesn't exist yet (and
`CliApp` is currently `internal`, so also expose it to the test project —
already covered by the existing `InternalsVisibleTo` in
`AssemblyInfo.cs`).

- [ ] **Step 3: Implement**

In `dotnet/src/EspBarcode.Viewer.Cli/CliApp.cs`, add:

```csharp
    public static IReadOnlyList<string> ListTypeWireValues()
        => Enum.GetValues<EspBarcode.Generator.BarcodeType>().Select(t => t.ToWireValue()).ToArray();
```

and in the `Run` method's `switch`, add a branch:

```csharp
                "list-types" => RunListTypes(),
```

with:

```csharp
    private static int RunListTypes()
    {
        foreach (var value in ListTypeWireValues()) Console.WriteLine(value);
        return 0;
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd dotnet && dotnet test tests/EspBarcode.Viewer.Cli.Tests --filter ListTypesTests`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add dotnet/
git commit -m "Add espbarcode-viewer list-types command"
```

---

### Task 13: CI to `windows-latest`, README updates, final solution-wide verification, push to `main`

**Files:**
- Modify: `.github/workflows/dotnet-ci.yml`
- Modify: `README.md`
- Modify: `dotnet/README.md`

**Interfaces:**
- Consumes: nothing new — this task wires and verifies everything from
  Tasks 1-12.

- [ ] **Step 1: Switch the CI runner to `windows-latest`**

In `.github/workflows/dotnet-ci.yml`, change:

```yaml
  build-and-test:
    name: Build & test
    runs-on: ubuntu-latest
```

to:

```yaml
  build-and-test:
    name: Build & test
    runs-on: windows-latest
```

Leave every other line (checkout, setup-dotnet, restore/build/test steps)
unchanged — they're OS-agnostic `dotnet` CLI invocations.

- [ ] **Step 2: Update `dotnet/README.md`**

Add a new section after the existing "## Running the demo CLI against
real hardware" section:

```markdown
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
```

Also update the "```text" project-layout block at the top of
`dotnet/README.md` to list the three new projects alongside
`EspBarcode.Client`/`EspBarcode.Cli`.

- [ ] **Step 3: Update the main `README.md`**

In the "## .NET client" section, add one sentence after the existing
paragraph pointing at the new standalone tool:

```markdown
A standalone generator/viewer (`EspBarcode.Viewer.Cli` +
`EspBarcode.Viewer.Gui`) replicates the ESP's barcode generation and
display behavior without any device connected — see
[`dotnet/README.md`](dotnet/README.md#standalone-generatorviewer-no-esp-required).
```

- [ ] **Step 4: Full solution build and test**

Run: `cd dotnet && dotnet restore EspScreenBarcodeGenerator.slnx && dotnet build EspScreenBarcodeGenerator.slnx --configuration Release && dotnet test EspScreenBarcodeGenerator.slnx --configuration Release`
Expected: every project builds, every test project passes (this must run
on Windows given `EspBarcode.Viewer.Gui`/`EspBarcode.Generator`'s
Windows-only dependencies — use the actual Windows dev machine this repo
is developed on per the environment context, not a Linux CI-simulation
shell).

- [ ] **Step 5: Repeat the Task 11 Step 7 manual smoke test end to end**

Run through: `generate qr ... --out`, `generate ... --open system`
(confirm the OS image viewer opens), `generate ... --open viewer` twice
with different specs against the same running viewer (confirm the window
content updates both times), resize the viewer window (confirm the
symbol re-lays-out, not just crops/stretches), `close` (confirm the
window exits). Fix any issue found here before proceeding — this is the
one point in the plan verifying real end-to-end behavior beyond unit
tests, per `superpowers:verification-before-completion`.

- [ ] **Step 6: Commit the CI/docs changes**

```bash
git add .github/workflows/dotnet-ci.yml README.md dotnet/README.md
git commit -m "Move .NET CI to windows-latest; document the standalone generator/viewer"
```

- [ ] **Step 7: Push to `main` and confirm CI is green**

```bash
git push origin main
```

Then check the `.NET CI` workflow run for this push (via `gh run list
--workflow=dotnet-ci.yml --limit 1` and `gh run watch <run-id>`, or the
GitHub Actions UI) and confirm it completes successfully. If it fails,
diagnose from the CI log (do not guess) and fix forward with a new
commit — do not force-push or amend.

---

## Self-Review Notes

- **Spec coverage:** every spec section has a task — core types (Task 2),
  check digits (3), all 14 symbologies including the documented Aztec
  Rune exclusion (4-5), screen-fit layout (6), payload files (7), PNG
  rendering (8), CLI generate/system-open (9), CLI viewer IPC (10), GUI
  window + Kestrel host (11), CLI polish (12), CI/docs/rollout (13).
- **Type consistency:** `BarcodeSpec`, `RawMatrix`, `RenderedLayout`,
  `BarcodeGenerationException`, `ViewerProtocol`, `OpenMode`,
  `ParsedGenerateCommand` are each defined exactly once (Tasks 1-2, 6, 9)
  and referenced with identical names/signatures in every later task.
- **No placeholders:** every step carries real, complete code; the two
  "adjust if X doesn't match ZXing.Net's exact API surface in this
  version" notes (Task 4 Step 3, Task 5 Step 2/4) are deliberate escape
  hatches for third-party API drift, not missing logic — they name the
  exact fallback action, not "handle it somehow."
