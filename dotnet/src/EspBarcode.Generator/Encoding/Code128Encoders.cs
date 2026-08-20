using ZXing;
using ZXing.OneD;

namespace EspBarcode.Generator.Encoding;

/// <summary>
/// Wraps ZXing.Net's Code 128 writer for both plain Code 128 and GS1-128 (which is Code 128 with a
/// leading FNC1 applied via <c>EncodeHintType.GS1_FORMAT</c>). Like <see cref="LinearEncoders"/>,
/// quiet zone is suppressed here (<c>MARGIN = 0</c>) because <c>ScreenFitLayout</c> (Task 7) applies
/// it later.
/// </summary>
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
