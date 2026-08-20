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
}
