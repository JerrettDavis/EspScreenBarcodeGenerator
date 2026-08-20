using EspBarcode.Generator.Encoding;
using ZXing;
using ZXing.Common;

namespace EspBarcode.Generator.Tests;

public class LinearEncodersTests
{
    /// <summary>
    /// ZXing.Net 0.16.11 has no test helper named "RGBLuminanceSourceFromBitMatrix" — that class
    /// lives only in ZXing.Net's own test suite, not the published package. Its public
    /// <c>ZXing.RGBLuminanceSource</c> (Gray8 byte-per-pixel constructor) does the same job, so we
    /// build the luminance bytes from the encoded RawMatrix ourselves.
    ///
    /// <c>LinearEncoders</c> deliberately encodes with <c>MARGIN = 0</c> (the quiet zone is
    /// ScreenFitLayout's job, Task 7), so the matrix under test has no white border. But ZXing's own
    /// 1D readers (HybridBinarizer and GlobalHistogramBinarizer alike) need a real quiet zone around
    /// the bars to locate the start/stop pattern — confirmed by probing: decoding a zero-margin image
    /// consistently returns null, while the same pixels surrounded by a white border decode correctly.
    /// So this test-only helper pads the synthesized image with a white quiet zone before decoding;
    /// that padding is a property of the *test image*, not of <see cref="RawMatrix"/> or the encoder.
    /// </summary>
    private static string Decode(RawMatrix matrix, BarcodeFormat format)
    {
        const int quietZone = 10;
        var width = matrix.Width + quietZone * 2;
        var height = matrix.Height + quietZone * 2;
        var luminancePixels = new byte[width * height];
        Array.Fill(luminancePixels, (byte)255);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                if (matrix[x, y])
                    luminancePixels[(y + quietZone) * width + (x + quietZone)] = 0;

        var hints = new Dictionary<DecodeHintType, object>
        {
            [DecodeHintType.POSSIBLE_FORMATS] = new List<BarcodeFormat> { format },
            // Codabar's reader strips the start/stop characters by default; this test asserts they
            // round-trip, matching ESP's documented "start/stop preserved" Codabar behavior.
            [DecodeHintType.RETURN_CODABAR_START_END] = true,
        };
        // MultiFormatOneDReader only honors hints passed to decode() itself, not just its
        // constructor (confirmed by probing) — pass them to both.
        var reader = new ZXing.OneD.MultiFormatOneDReader(hints);
        var luminance = new RGBLuminanceSource(luminancePixels, width, height, RGBLuminanceSource.BitmapFormat.Gray8);
        var binarizer = new HybridBinarizer(luminance);
        var binaryBitmap = new BinaryBitmap(binarizer);
        var result = reader.decode(binaryBitmap, hints);
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
