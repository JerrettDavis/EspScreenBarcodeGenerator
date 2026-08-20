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

    // ZXing throws its own exception types straight out of writer.encode for a payload it cannot
    // represent. Encode has to translate them, or every caller has to know about ZXing: the CLI
    // catches only BarcodeGenerationException and would otherwise die with a raw stack trace.
    // ArgumentException and WriterException are the two types that were observed escaping when all
    // 14 encoders were probed with malformed input; one case of each is covered here.
    [Theory]
    [InlineData(BarcodeType.Code39, "héllo!*%")]      // ArgumentException: non-encodable character
    [InlineData(BarcodeType.Codabar, "abc-def")]      // ArgumentException: cannot encode
    [InlineData(BarcodeType.Code128, "日本語テキスト")]   // ArgumentException: bad character in input
    [InlineData(BarcodeType.Qr, "")]                  // ArgumentException: empty contents
    [InlineData(BarcodeType.Pdf417, "日本語テキスト")]    // ZXing.WriterException: non-encodable character
    public void Encode_PayloadZXingCannotRepresent_ThrowsInvalidPayload(BarcodeType type, string data)
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => BarcodeGenerator.Encode(new BarcodeSpec { Type = type, Data = data }));
        Assert.Equal("invalid_payload", ex.Code);
        Assert.NotEmpty(ex.Message); // the underlying reason is carried through, not discarded
    }

    [Fact]
    public void Encode_PayloadRejectedByChecksums_KeepsItsOwnMoreSpecificCode()
    {
        // The wrapping catch must not swallow the codes the generator already reports itself. Both
        // halves deliberately use a code the generic wrapper cannot produce: it only ever reports
        // "invalid_payload", so asserting that code here would pass whether or not the specific
        // exception survived. A UPC-A body with the wrong final digit reaches Checksums, which
        // reports "invalid_checksum".
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            BarcodeGenerator.Encode(new BarcodeSpec { Type = BarcodeType.UpcA, Data = "036000291459" }));
        Assert.Equal("invalid_checksum", ex.Code);

        var qr = Assert.Throws<BarcodeGenerationException>(() =>
            BarcodeGenerator.Encode(new BarcodeSpec { Type = BarcodeType.Qr, Data = new string('X', 5000), QrMaxVersion = 5 }));
        Assert.Equal("data_too_long", qr.Code);
    }
}
