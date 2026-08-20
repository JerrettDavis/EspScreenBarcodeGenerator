namespace EspBarcode.Generator.Tests;

public class ScreenFitLayoutTests
{
    private static RawMatrix SquareMatrix(int size)
    {
        var m = new RawMatrix(size, size);
        for (var y = 0; y < size; y++)
            for (var x = 0; x < size; x++)
                m[x, y] = (x + y) % 2 == 0;
        return m;
    }

    [Fact]
    public void ResolveQuietZone_ExplicitValue_ReturnedUnchanged()
    {
        Assert.Equal(7, ScreenFitLayout.ResolveQuietZone(BarcodeType.Qr, 7));
    }

    [Fact]
    public void ResolveQuietZone_MatrixDefault_IsFour()
    {
        Assert.Equal(4, ScreenFitLayout.ResolveQuietZone(BarcodeType.DataMatrix, -1));
    }

    [Fact]
    public void ResolveQuietZone_LinearDefault_IsTen()
    {
        Assert.Equal(10, ScreenFitLayout.ResolveQuietZone(BarcodeType.Code128, -1));
    }

    [Fact]
    public void Fit_SquareMatrixInSquareCanvas_PicksLargestIntegerScale()
    {
        // 21x21 symbol + 4-module quiet zone each side = 29 logical modules.
        // 480 / 29 = 16.5..., so the largest integer scale is 16.
        var layout = ScreenFitLayout.Fit(SquareMatrix(21), quiet: 4, minModule: 2, rotation: "0", canvasWidth: 480, canvasHeight: 480);
        Assert.Equal(16, layout.Scale);
    }

    [Fact]
    public void Fit_Auto_PicksBetterFittingOrientationForWideMatrix()
    {
        // 100-wide x 10-tall linear symbol in a 320x480 portrait canvas:
        // unrotated: logicalWidth=100+20=120, logicalHeight=10+20=30 -> scale = min(320/120, 480/30) = min(2, 16) = 2.
        // rotated: logicalWidth=10+20=30, logicalHeight=100+20=120 -> scale = min(320/30, 480/120) = min(10, 4) = 4.
        // Rotated wins (4 > 2).
        var matrix = new RawMatrix(100, 10);
        var layout = ScreenFitLayout.Fit(matrix, quiet: 10, minModule: 1, rotation: "auto", canvasWidth: 320, canvasHeight: 480);
        Assert.Equal(100, layout.Matrix.Height); // rotated: original width becomes height
        Assert.Equal(10, layout.Matrix.Width);
    }

    [Fact]
    public void Fit_TooDenseForCanvas_ThrowsTooDense()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            ScreenFitLayout.Fit(SquareMatrix(500), quiet: 4, minModule: 8, rotation: "0", canvasWidth: 320, canvasHeight: 480));
        Assert.Equal("too_dense", ex.Code);
    }

    [Fact]
    public void Fit_ExplicitRotation0_NeverRotates()
    {
        var matrix = new RawMatrix(100, 10);
        var layout = ScreenFitLayout.Fit(matrix, quiet: 10, minModule: 1, rotation: "0", canvasWidth: 320, canvasHeight: 480);
        Assert.Equal(100, layout.Matrix.Width);
        Assert.Equal(10, layout.Matrix.Height);
    }

    [Fact]
    public void Fit_TwoDimensionalMatrix_ReportsNoBarModules()
    {
        var layout = ScreenFitLayout.Fit(SquareMatrix(21), quiet: 4, minModule: 2, rotation: "0", canvasWidth: 480, canvasHeight: 480);
        Assert.Equal(0, layout.BarModules);
    }

    [Fact]
    public void Fit_SingleRowMatrix_ScalesAndCentresAgainstThe48ModuleBarLength()
    {
        // A linear symbology encodes to one row of bars; its drawn length is a fixed 48 modules
        // (firmware EspBarcodeCore.cpp candidate()), not the literal matrix height of 1.
        // 68 bars + 10 quiet each side = 88 logical wide; 48 + 20 = 68 logical tall.
        // scale = min(320/88, 480/68) = min(3, 7) = 3.
        // offsetY = (480 - 68*3) / 2 = 138.  Laying out against the literal height of 1 would
        // instead give a 21-module logical height and offsetY = (480 - 21*3) / 2 = 208.
        var layout = ScreenFitLayout.Fit(new RawMatrix(68, 1), quiet: 10, minModule: 1, rotation: "0", canvasWidth: 320, canvasHeight: 480);

        Assert.Equal(ScreenFitLayout.LinearBarModules, layout.BarModules);
        Assert.Equal(3, layout.Scale);
        Assert.Equal(28, layout.OffsetXPixels);  // (320 - 88*3) / 2
        Assert.Equal(138, layout.OffsetYPixels);
    }

    [Fact]
    public void Fit_SingleRowMatrixAuto_PicksAScaleThatKeepsTheWholeBarLengthOnCanvas()
    {
        // The regression this guards: scored against a literal height of 1, the rotated candidate
        // wins with scale 5, and bars 48 modules long then need (48 + 20) * 5 = 340 pixels across a
        // 320-pixel canvas — the symbol runs off the edge. Substituting 48 before the orientation
        // choice brings the winning scale down to one the canvas can actually hold.
        var layout = ScreenFitLayout.Fit(new RawMatrix(68, 1), quiet: 10, minModule: 1, rotation: "auto", canvasWidth: 320, canvasHeight: 480);

        Assert.Equal(ScreenFitLayout.LinearBarModules, layout.BarModules);

        var rotated = layout.Matrix.Width == 1;
        var barCount = rotated ? layout.Matrix.Height : layout.Matrix.Width;
        var barLengthPixels = layout.BarModules * layout.Scale;
        var acrossPixels = barCount * layout.Scale;
        var quietPixels = 2 * layout.QuietModules * layout.Scale;

        var usedWidth = (rotated ? barLengthPixels : acrossPixels) + quietPixels;
        var usedHeight = (rotated ? acrossPixels : barLengthPixels) + quietPixels;

        Assert.True(usedWidth <= layout.CanvasWidth, $"symbol is {usedWidth}px wide on a {layout.CanvasWidth}px canvas");
        Assert.True(usedHeight <= layout.CanvasHeight, $"symbol is {usedHeight}px tall on a {layout.CanvasHeight}px canvas");
        Assert.True(layout.OffsetXPixels >= 0);
        Assert.True(layout.OffsetYPixels >= 0);
    }

    [Fact]
    public void Fit_SingleRowMatrixTooDense_ReportsTheConceptualBarLengthNotTheLiteralHeight()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            ScreenFitLayout.Fit(new RawMatrix(400, 1), quiet: 10, minModule: 8, rotation: "0", canvasWidth: 320, canvasHeight: 480));

        Assert.Equal("too_dense", ex.Code);
        Assert.Contains("400x48", ex.Message);
    }
}
