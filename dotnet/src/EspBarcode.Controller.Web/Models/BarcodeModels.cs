namespace EspBarcode.Controller.Web.Models;

/// <summary>Wire-value symbologies from docs/PROTOCOL.md's <c>generate</c> command.</summary>
public enum BarcodeKind
{
    Qr, DataMatrix, Aztec, Code128, Gs1_128, Code39, UpcA, Ean13, Ean8, Itf, Itf14, Codabar, Msi,
}

public static class BarcodeKindExtensions
{
    public static string ToWireValue(this BarcodeKind kind) => kind switch
    {
        BarcodeKind.Qr => "qr",
        BarcodeKind.DataMatrix => "datamatrix",
        BarcodeKind.Aztec => "aztec",
        BarcodeKind.Code128 => "code128",
        BarcodeKind.Gs1_128 => "gs1-128",
        BarcodeKind.Code39 => "code39",
        BarcodeKind.UpcA => "upca",
        BarcodeKind.Ean13 => "ean13",
        BarcodeKind.Ean8 => "ean8",
        BarcodeKind.Itf => "itf",
        BarcodeKind.Itf14 => "itf14",
        BarcodeKind.Codabar => "codabar",
        BarcodeKind.Msi => "msi",
        _ => throw new ArgumentOutOfRangeException(nameof(kind)),
    };

    public static string DisplayName(this BarcodeKind kind) => kind switch
    {
        BarcodeKind.Qr => "QR Code",
        BarcodeKind.DataMatrix => "Data Matrix",
        BarcodeKind.Aztec => "Aztec",
        BarcodeKind.Code128 => "Code 128",
        BarcodeKind.Gs1_128 => "GS1-128",
        BarcodeKind.Code39 => "Code 39",
        BarcodeKind.UpcA => "UPC-A",
        BarcodeKind.Ean13 => "EAN-13",
        BarcodeKind.Ean8 => "EAN-8",
        BarcodeKind.Itf => "ITF",
        BarcodeKind.Itf14 => "ITF-14",
        BarcodeKind.Codabar => "Codabar",
        BarcodeKind.Msi => "MSI",
        _ => kind.ToString(),
    };

    public static readonly BarcodeKind[] All =
    [
        BarcodeKind.Qr, BarcodeKind.DataMatrix, BarcodeKind.Aztec, BarcodeKind.Code128, BarcodeKind.Gs1_128,
        BarcodeKind.Code39, BarcodeKind.UpcA, BarcodeKind.Ean13, BarcodeKind.Ean8, BarcodeKind.Itf,
        BarcodeKind.Itf14, BarcodeKind.Codabar, BarcodeKind.Msi,
    ];
}

/// <summary>Every field the device's <c>generate</c> command accepts, host-side representation.</summary>
public sealed class GenerateOptions
{
    public BarcodeKind Type { get; set; } = BarcodeKind.Qr;
    public string Data { get; set; } = "";
    public bool Display { get; set; } = true;
    public string? SaveAs { get; set; }
    public string Ecc { get; set; } = "M";
    public string Rotation { get; set; } = "auto";
    public int Quiet { get; set; } = -1;
    public int MinModule { get; set; } = 2;
    public bool Rectangular { get; set; }
    public bool Invert { get; set; }
    public bool Checksum { get; set; } = true;
    public int QrMinVersion { get; set; } = 1;
    public int QrMaxVersion { get; set; } = 20;
    public int AztecSecurity { get; set; } = 23;
    public int AztecLayers { get; set; } = 1;

    public GenerateOptions Clone() => (GenerateOptions)MemberwiseClone();
}

public sealed record GenerateResult(
    string Type, int Width, int Height, bool Linear, int Quiet, bool Displayed, string NormalizedData);

public sealed record DeviceInfo(string Device, string Firmware, string Protocol, string Transport, int ScreenWidth, int ScreenHeight);

public sealed record StatusInfo(bool BarcodeVisible, bool HasCurrent, bool CurrentRaw, string Status, long FreeHeap);

public sealed record DownloadedMatrix(int Width, int Height, bool Linear, bool Invert, string Label, byte[] Packed);

/// <summary>A barcode spec saved in the browser's local library (see <c>BarcodeLibraryService</c>).</summary>
public sealed class LibraryItem
{
    public string Id { get; set; } = Guid.NewGuid().ToString("n");
    public string Name { get; set; } = "";
    public GenerateOptions Options { get; set; } = new();
    public DateTimeOffset CreatedAt { get; set; } = DateTimeOffset.UtcNow;
}
