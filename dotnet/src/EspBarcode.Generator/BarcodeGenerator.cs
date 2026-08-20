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

        try
        {
            return Dispatch(spec);
        }
        // ZXing.Net reports every payload it cannot encode - empty data, characters outside the
        // symbology's alphabet, data longer than the symbology allows - by throwing out of
        // writer.encode. Probing all 14 encoders with malformed input showed exactly two types
        // escape: ArgumentException (most writers) and WriterException (PDF417). Translating them
        // here, at the single choke point, is what lets every caller handle a bad payload through
        // the one BarcodeGenerationException contract instead of crashing on a raw ZXing type.
        // ArgumentOutOfRangeException is excluded so Dispatch's unknown-symbology guard below - a
        // programming error, not bad user data - is never mislabelled as a payload problem.
        catch (Exception ex) when (ex is ZXing.WriterException || (ex is ArgumentException and not ArgumentOutOfRangeException))
        {
            throw new BarcodeGenerationException("invalid_payload", ex.Message);
        }
    }

    private static RawMatrix Dispatch(BarcodeSpec spec) =>
        spec.Type switch
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
