namespace EspBarcode.Client;

/// <summary>Symbologies the device can generate on-board (protocol 1.0 <c>capabilities.symbologies</c>).</summary>
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
        _ => throw new ArgumentOutOfRangeException(nameof(type), type, "Unknown barcode type"),
    };
}
