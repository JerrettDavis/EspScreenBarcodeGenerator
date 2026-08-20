namespace EspBarcode.Generator.Tests;

public class BarcodeGeneratorTests
{
    [Theory]
    [InlineData(BarcodeType.Qr, "LAB-TEST-001")]
    [InlineData(BarcodeType.Code128, "LOT-2026-00042")]
    [InlineData(BarcodeType.UpcA, "03600029145")]
    public void Encode_DispatchesWithoutThrowing(BarcodeType type, string data)
    {
        var matrix = BarcodeGenerator.Encode(new BarcodeSpec { Type = type, Data = data });
        Assert.True(matrix.Width > 0);
        Assert.True(matrix.Height > 0);
    }

    [Fact]
    public void Encode_AztecRune_ThrowsUnsupported()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Aztec, Data = "42", AztecLayers = 0 };
        var ex = Assert.Throws<BarcodeGenerationException>(() => BarcodeGenerator.Encode(spec));
        Assert.Equal("aztec_rune_unsupported", ex.Code);
    }
}
