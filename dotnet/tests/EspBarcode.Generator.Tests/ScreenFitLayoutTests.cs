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
}
