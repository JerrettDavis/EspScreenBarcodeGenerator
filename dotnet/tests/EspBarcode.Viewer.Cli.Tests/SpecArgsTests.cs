using EspBarcode.Generator;
using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class SpecArgsTests
{
    [Fact]
    public void Parse_MinimalGenerateCommand_UsesDefaults()
    {
        var parsed = SpecArgs.Parse(["generate", "qr", "LAB-TEST-001"]);
        Assert.Equal(BarcodeType.Qr, parsed.Spec.Type);
        Assert.Equal("LAB-TEST-001", parsed.Spec.Data);
        Assert.Null(parsed.OutPath);
        Assert.Equal(OpenMode.None, parsed.Open);
    }

    [Fact]
    public void Parse_AllOptions_MapsToSpecFields()
    {
        var parsed = SpecArgs.Parse([
            "generate", "datamatrix", "DM-TEST",
            "--out", "out.png",
            "--open", "system",
            "--ecc", "H",
            "--rotation", "90",
            "--quiet", "6",
            "--min-module", "3",
            "--rect",
            "--invert",
            "--no-checksum",
            "--qr-min-version", "2",
            "--qr-max-version", "10",
            "--aztec-security", "30",
            "--aztec-layers", "4",
        ]);

        Assert.Equal(BarcodeType.DataMatrix, parsed.Spec.Type);
        Assert.Equal("out.png", parsed.OutPath);
        Assert.Equal(OpenMode.System, parsed.Open);
        Assert.Equal("H", parsed.Spec.Ecc);
        Assert.Equal("90", parsed.Spec.Rotation);
        Assert.Equal(6, parsed.Spec.Quiet);
        Assert.Equal(3, parsed.Spec.MinModule);
        Assert.True(parsed.Spec.Rectangular);
        Assert.True(parsed.Spec.Invert);
        Assert.False(parsed.Spec.Checksum);
        Assert.Equal(2, parsed.Spec.QrMinVersion);
        Assert.Equal(10, parsed.Spec.QrMaxVersion);
        Assert.Equal(30, parsed.Spec.AztecSecurity);
        Assert.Equal(4, parsed.Spec.AztecLayers);
    }

    [Fact]
    public void Parse_OpenViewer_SetsOpenModeViewer()
    {
        var parsed = SpecArgs.Parse(["generate", "code128", "LOT-1", "--open", "viewer"]);
        Assert.Equal(OpenMode.Viewer, parsed.Open);
    }

    [Fact]
    public void Parse_UnknownType_ThrowsInvalidArgs()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => SpecArgs.Parse(["generate", "not-a-type", "DATA"]));
        Assert.Equal("invalid_args", ex.Code);
    }

    [Fact]
    public void Parse_MissingData_ThrowsInvalidArgs()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => SpecArgs.Parse(["generate", "qr"]));
        Assert.Equal("invalid_args", ex.Code);
    }

    [Theory]
    [InlineData("--quiet")]
    [InlineData("--min-module")]
    [InlineData("--qr-min-version")]
    [InlineData("--qr-max-version")]
    [InlineData("--aztec-security")]
    [InlineData("--aztec-layers")]
    public void Parse_NonNumericValueForNumericOption_ThrowsInvalidArgs(string option)
    {
        // int.Parse threw a bare FormatException here, which CliApp's BarcodeGenerationException
        // handler could not see: the user got a stack trace for a plain typo.
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            SpecArgs.Parse(["generate", "qr", "HELLO", option, "abc"]));

        Assert.Equal("invalid_args", ex.Code);
        Assert.Contains(option, ex.Message);
        Assert.Contains("abc", ex.Message);
    }

    [Fact]
    public void Parse_UnknownOption_ThrowsInvalidArgs()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            SpecArgs.Parse(["generate", "qr", "HELLO", "--nonsense-flag"]));

        Assert.Equal("invalid_args", ex.Code);
        Assert.Contains("--nonsense-flag", ex.Message);
    }

    [Fact]
    public void Parse_MisspelledOption_ThrowsInsteadOfSilentlyIgnoringIt()
    {
        // The trap this guards: --rotaton 90 used to leave Rotation at "auto" and exit 0, so a
        // scanner test silently ran against a different symbol than the one that was asked for.
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            SpecArgs.Parse(["generate", "qr", "HELLO", "--rotaton", "90"]));

        Assert.Equal("invalid_args", ex.Code);
        Assert.Contains("--rotaton", ex.Message);
    }

    [Fact]
    public void Parse_ViewerOptions_AreAcceptedEvenThoughSpecArgsDoesNotConsumeThem()
    {
        // ViewerClient reads these off the same argv later; neither the names nor their values are
        // "unknown" just because they do not land in the BarcodeSpec.
        var parsed = SpecArgs.Parse([
            "generate", "qr", "HELLO",
            "--open", "viewer",
            "--viewer-port", "47999",
            "--viewer-exe", "C:\\viewer\\EspBarcode.Viewer.Gui.exe",
        ]);

        Assert.Equal(OpenMode.Viewer, parsed.Open);
        Assert.Equal("HELLO", parsed.Spec.Data);
    }

    [Fact]
    public void Parse_OptionWithoutValue_ThrowsInvalidArgs()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            SpecArgs.Parse(["generate", "qr", "HELLO", "--rotation"]));

        Assert.Equal("invalid_args", ex.Code);
        Assert.Contains("--rotation", ex.Message);
    }

    [Fact]
    public void Parse_NegativeQuiet_IsStillAcceptedAsTheSymbologyDefaultSentinel()
    {
        var parsed = SpecArgs.Parse(["generate", "qr", "HELLO", "--quiet", "-1"]);
        Assert.Equal(-1, parsed.Spec.Quiet);
    }
}
