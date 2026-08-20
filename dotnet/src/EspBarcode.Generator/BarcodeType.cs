using System.Text.Json.Serialization;

namespace EspBarcode.Generator;

/// <summary>Symbologies the standalone generator supports. Wire values match docs/PROTOCOL.md's generate.type field for the 13 shared with the ESP, plus pdf417 (host-only, matching the existing "license" scenario).</summary>
/// <remarks>The converter makes JSON use those same wire values, so the viewer's <c>POST /render</c>
/// body speaks the documented vocabulary rather than enum ordinals — see <see cref="BarcodeTypeJsonConverter"/>.</remarks>
[JsonConverter(typeof(BarcodeTypeJsonConverter))]
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
