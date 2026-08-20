namespace EspBarcode.Generator.Tests;

public class BarcodeTypeTests
{
    [Theory]
    [InlineData(BarcodeType.Qr, "qr")]
    [InlineData(BarcodeType.DataMatrix, "datamatrix")]
    [InlineData(BarcodeType.Aztec, "aztec")]
    [InlineData(BarcodeType.Code128, "code128")]
    [InlineData(BarcodeType.Gs1_128, "gs1-128")]
    [InlineData(BarcodeType.Code39, "code39")]
    [InlineData(BarcodeType.UpcA, "upca")]
    [InlineData(BarcodeType.Ean13, "ean13")]
    [InlineData(BarcodeType.Ean8, "ean8")]
    [InlineData(BarcodeType.Itf, "itf")]
    [InlineData(BarcodeType.Itf14, "itf14")]
    [InlineData(BarcodeType.Codabar, "codabar")]
    [InlineData(BarcodeType.Msi, "msi")]
    [InlineData(BarcodeType.Pdf417, "pdf417")]
    public void ToWireValue_And_ParseWireValue_RoundTrip(BarcodeType type, string wire)
    {
        Assert.Equal(wire, type.ToWireValue());
        Assert.Equal(type, BarcodeTypeExtensions.ParseWireValue(wire));
    }

    [Fact]
    public void ParseWireValue_UnknownValue_ThrowsWithUnknownTypeCode()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => BarcodeTypeExtensions.ParseWireValue("not-a-type"));
        Assert.Equal("unknown_type", ex.Code);
    }
}
