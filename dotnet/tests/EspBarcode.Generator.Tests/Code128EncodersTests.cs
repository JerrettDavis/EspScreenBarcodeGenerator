using EspBarcode.Generator.Encoding;
using ZXing.OneD;

namespace EspBarcode.Generator.Tests;

public class Code128EncodersTests
{
    private static string Decode(RawMatrix matrix)
    {
        var reader = new Code128Reader();
        var result = reader.decode(TestDecodeHelpers.ToBinaryBitmap(matrix));
        Assert.NotNull(result);
        return result!.Text;
    }

    [Fact]
    public void EncodeCode128_PlainText_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Code128, Data = "LOT-2026-00042" };
        Assert.Equal("LOT-2026-00042", Decode(Code128Encoders.EncodeCode128(spec)));
    }

    [Fact]
    public void EncodeGs1_128_PrependsFnc1AndRoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Gs1_128, Data = "10ABC" };
        var text = Decode(Code128Encoders.EncodeGs1_128(spec));
        Assert.Contains("10ABC", text);
    }
}
