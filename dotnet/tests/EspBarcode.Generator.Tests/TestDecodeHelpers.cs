using ZXing;
using ZXing.Common;

namespace EspBarcode.Generator.Tests;

/// <summary>
/// Shared test-only helper for decoding a zero-margin <see cref="RawMatrix"/> with ZXing.Net's
/// matrix/Code128 readers. ZXing.Net 0.16.11 has no test helper named
/// "RGBLuminanceSourceFromBitMatrix" — that class lives only in ZXing.Net's own test suite, not the
/// published package. Its public <see cref="RGBLuminanceSource"/> (Gray8 byte-per-pixel constructor)
/// does the same job, so this builds the luminance bytes from the encoded <see cref="RawMatrix"/>
/// directly (confirmed absent via reflection over the installed 0.16.11 package — same conclusion
/// Task 4 reached for <c>LinearEncoders</c>).
///
/// <c>Code128Encoders</c>/<c>MatrixEncoders</c> deliberately encode with <c>MARGIN = 0</c> (the quiet
/// zone is <c>ScreenFitLayout</c>'s job, Task 7), so the matrix under test has no white border. A
/// white quiet zone is synthesized around the image before decoding purely as a property of the
/// *test image* — never baked into <see cref="RawMatrix"/> or the encoders themselves. This is the
/// 2D/Code128 counterpart of <c>LinearEncodersTests.Decode</c>'s equivalent padding for 1D readers
/// (which additionally needs <c>MultiFormatOneDReader</c> + format hints, so it keeps its own
/// private helper rather than sharing this one).
///
/// Each logical module is also rendered at <paramref name="moduleScale"/> physical pixels (default
/// 4, not 1): probing showed QRCodeReader/DataMatrixReader/AztecReader's finder-pattern detectors
/// reliably return null against a 1-pixel-per-module image (even with a generous quiet zone) but
/// decode correctly once modules are a few pixels wide — the same one-module-per-pixel image these
/// readers' own detectors are tuned against a camera-resolution symbol, not a minimal synthetic one.
/// Code 128's 1D reader has no such requirement but is unaffected by the extra scale.
/// </summary>
internal static class TestDecodeHelpers
{
    public static BinaryBitmap ToBinaryBitmap(RawMatrix matrix, int quietZoneModules = 10, int moduleScale = 4)
    {
        var quietZone = quietZoneModules * moduleScale;
        var width = matrix.Width * moduleScale + quietZone * 2;
        var height = matrix.Height * moduleScale + quietZone * 2;
        var luminancePixels = new byte[width * height];
        Array.Fill(luminancePixels, (byte)255);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                if (matrix[x, y])
                    for (var dy = 0; dy < moduleScale; dy++)
                        for (var dx = 0; dx < moduleScale; dx++)
                            luminancePixels[(y * moduleScale + dy + quietZone) * width + (x * moduleScale + dx + quietZone)] = 0;

        var luminance = new RGBLuminanceSource(luminancePixels, width, height, RGBLuminanceSource.BitmapFormat.Gray8);
        return new BinaryBitmap(new HybridBinarizer(luminance));
    }
}
