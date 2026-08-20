using EspBarcode.Cli;

namespace EspBarcode.Client.Tests;

/// <summary>
/// Covers the "generate a license barcode" demo scenario end to end at the
/// encoding layer: PDF417 (which the firmware can't generate on-board) is
/// rendered on the host via ZXing.Net, then packed exactly as it would be
/// sent through the raw-matrix upload protocol.
/// </summary>
public class LicenseBarcodeScenarioTests
{
    [Fact]
    public void EncodePdf417_ProducesANonTrivialModuleMatrix()
    {
        var matrix = Scenarios.EncodePdf417(Scenarios.BuildSyntheticLicensePayload());

        Assert.True(matrix.Width > 20);
        Assert.True(matrix.Height > 5);
    }

    [Fact]
    public void EncodePdf417_PacksWithoutThrowingAndFitsDeviceMatrixLimits()
    {
        var matrix = Scenarios.EncodePdf417(Scenarios.BuildSyntheticLicensePayload());

        var packed = matrix.Pack();

        Assert.Equal((matrix.Width * matrix.Height + 7) / 8, packed.Length);
        // capabilities.limits.matrix_width/matrix_height are both 512 (docs/PROTOCOL.md).
        Assert.True(matrix.Width <= 512);
        Assert.True(matrix.Height <= 512);
    }

    [Fact]
    public void EncodePdf417_RoundTripsThroughRawMatrixPacking()
    {
        var matrix = Scenarios.EncodePdf417(Scenarios.BuildSyntheticLicensePayload());

        var restored = RawMatrix.Unpack(matrix.Width, matrix.Height, matrix.Pack());

        for (var y = 0; y < matrix.Height; y++)
        {
            for (var x = 0; x < matrix.Width; x++)
            {
                Assert.Equal(matrix[x, y], restored[x, y]);
            }
        }
    }

    [Fact]
    public void BuildSyntheticLicensePayload_CarriesTheAamvaAnsiHeaderShape()
    {
        var payload = Scenarios.BuildSyntheticLicensePayload();

        Assert.Contains("ANSI ", payload);
        Assert.Contains("DAQD1234567890123", payload); // synthetic license number subfield
        Assert.Contains("DCSDOE", payload); // synthetic last name subfield
    }
}
