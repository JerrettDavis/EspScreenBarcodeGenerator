using ZXing;
using ZXing.Aztec;
using ZXing.Datamatrix;
using ZXing.Datamatrix.Encoder;
using ZXing.PDF417;
using ZXing.QrCode;
using ZXing.QrCode.Internal;

namespace EspBarcode.Generator.Encoding;

/// <summary>
/// Wraps ZXing.Net's matrix-family writers (QR, Data Matrix, Aztec, PDF417). Like
/// <see cref="LinearEncoders"/> and <see cref="Code128Encoders"/>, quiet zone is suppressed here
/// (<c>MARGIN = 0</c>) because <c>ScreenFitLayout</c> (Task 7) applies it later.
/// </summary>
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
