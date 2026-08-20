using ZXing;
using ZXing.OneD;

namespace EspBarcode.Generator.Encoding;

/// <summary>
/// Wraps ZXing.Net's linear/checksum-family writers (UPC-A, EAN-13/8, ITF, ITF-14, MSI,
/// Code 39, Codabar). Payload normalization (check digits, padding, start/stop characters)
/// goes through <see cref="Checksums"/> first, so the writer always receives a fully-formed
/// payload. The quiet zone is intentionally suppressed here (<c>EncodeHintType.MARGIN = 0</c>)
/// because <c>ScreenFitLayout</c> (Task 7) applies it later.
/// </summary>
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
