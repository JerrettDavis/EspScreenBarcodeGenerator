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
