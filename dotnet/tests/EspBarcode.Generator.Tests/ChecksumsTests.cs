namespace EspBarcode.Generator.Tests;

public class ChecksumsTests
{
    [Fact]
    public void NormalizeUpcA_ElevenDigits_ComputesCheckDigit()
    {
        // Known-valid UPC-A: 036000291452 (Tic Tac, commonly cited example)
        Assert.Equal("036000291452", Checksums.NormalizeUpcA("03600029145", checksum: true));
    }

    [Fact]
    public void NormalizeUpcA_TwelveDigits_ValidatesExistingCheckDigit()
    {
        Assert.Equal("036000291452", Checksums.NormalizeUpcA("036000291452", checksum: true));
    }

    [Fact]
    public void NormalizeUpcA_TwelveDigits_WrongCheckDigit_Throws()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => Checksums.NormalizeUpcA("036000291459", checksum: true));
        Assert.Equal("invalid_checksum", ex.Code);
    }

    [Fact]
    public void NormalizeEan13_TwelveDigits_ComputesCheckDigit()
    {
        // Wikipedia's canonical EAN-13 example: 4006381333931
        Assert.Equal("4006381333931", Checksums.NormalizeEan13("400638133393", checksum: true));
    }

    [Fact]
    public void NormalizeEan8_SevenDigits_ComputesCheckDigit()
    {
        // Wikipedia's canonical EAN-8 example: 96385074
        Assert.Equal("96385074", Checksums.NormalizeEan8("9638507", checksum: true));
    }

    [Fact]
    public void NormalizeItf14_ThirteenDigits_ComputesCheckDigit()
    {
        Assert.Equal("12345678901231", Checksums.NormalizeItf14("1234567890123", checksum: true));
    }

    [Fact]
    public void NormalizeItf_OddLength_LeftPadsZero()
    {
        Assert.Equal("0123", Checksums.NormalizeItf("123"));
        Assert.Equal("1234", Checksums.NormalizeItf("1234"));
    }

    [Fact]
    public void NormalizeMsi_ChecksumTrue_AppendsLuhnDigit()
    {
        // Hand-computed Luhn digit for 1234567 is 4.
        Assert.Equal("12345674", Checksums.NormalizeMsi("1234567", checksum: true));
    }

    [Fact]
    public void NormalizeMsi_ChecksumFalse_LeavesDataUnchanged()
    {
        Assert.Equal("1234567", Checksums.NormalizeMsi("1234567", checksum: false));
    }

    [Theory]
    [InlineData("123", "A123A")]
    [InlineData("A123B", "A123B")]
    [InlineData("a123b", "a123b")]
    public void NormalizeCodabar_AddsStartStopWhenOmitted(string input, string expected)
    {
        Assert.Equal(expected, Checksums.NormalizeCodabar(input));
    }

    [Theory]
    [InlineData("{FNC1}010", "ñ010")]
    [InlineData("<FNC1>010", "ñ010")]
    [InlineData("<GS>010", "ñ010")]
    [InlineData("010", "ñ010")]
    public void NormalizeFnc1Tokens_ReplacesAllRecognizedTokens(string input, string expected)
    {
        Assert.Equal(expected, Checksums.NormalizeFnc1Tokens(input));
    }
}