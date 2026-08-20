using ZXing.QrCode;

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

    // ---- min_module 0 -------------------------------------------------------------------------

    [Fact]
    public void Fit_MinModuleZeroWithRoomToFit_ClampsToOneAndNeverReturnsScaleZero()
    {
        // min_module 0 used to make the too-dense guard vacuous (`scale < 0` is never true), so a
        // scale of 0 sailed through and every module drew as a zero-sized rectangle: a blank PNG
        // with exit code 0. The firmware clamps the minimum to 1 before scoring (EspBarcodeCore.cpp
        // calculateLayout).
        var layout = ScreenFitLayout.Fit(SquareMatrix(21), quiet: 4, minModule: 0, rotation: "0", canvasWidth: 480, canvasHeight: 480);

        Assert.Equal(16, layout.Scale); // identical to the same call with min_module 1 or 2
        Assert.True(layout.Scale >= 1);
    }

    [Fact]
    public void Fit_MinModuleZeroButSymbolLargerThanCanvas_ThrowsTooDenseInsteadOfScaleZero()
    {
        // 500 bars plus quiet zones need 520 logical modules across a 40-pixel canvas, so the
        // integer scale computes to exactly 0 — the case that used to slip past `scale < minModule`.
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            ScreenFitLayout.Fit(new RawMatrix(500, 1), quiet: 10, minModule: 0, rotation: "0", canvasWidth: 40, canvasHeight: 40));

        Assert.Equal("too_dense", ex.Code);
    }

    [Fact]
    public void Fit_MinModuleNegative_IsAlsoClampedToOne()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() =>
            ScreenFitLayout.Fit(SquareMatrix(500), quiet: 4, minModule: -5, rotation: "0", canvasWidth: 40, canvasHeight: 40));

        Assert.Equal("too_dense", ex.Code);
    }

    // ---- the four orientations ----------------------------------------------------------------

    /// <summary>A 3x2 matrix with exactly one set module, in the top-right corner. Every rotation
    /// moves it to a different, hand-checkable corner.</summary>
    private static RawMatrix CornerMark2D()
    {
        var m = new RawMatrix(3, 2);
        m[2, 0] = true;
        return m;
    }

    private static (int X, int Y) SoleSetModule(RawMatrix matrix)
    {
        (int X, int Y)? found = null;
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                if (matrix[x, y])
                {
                    Assert.Null(found); // a rotation must not duplicate or drop modules
                    found = (x, y);
                }
        Assert.NotNull(found);
        return found!.Value;
    }

    [Theory]
    // Firmware src/BarcodeApplication.cpp renderCurrent(), 2D module loop:
    //   Deg90:  rx = height - 1 - moduleY; ry = moduleX
    //   Deg180: rx = width  - 1 - moduleX; ry = height - 1 - moduleY
    //   Deg270: rx = moduleY;              ry = width  - 1 - moduleX
    // For the mark at (2,0) of a 3x2 matrix that is: unmoved / bottom-right of a 2x3 /
    // bottom-left of a 3x2 / top-left of a 2x3 — i.e. the corner a real clockwise, half and
    // counter-clockwise turn would carry it to.
    [InlineData("0", 3, 2, 2, 0)]
    [InlineData("90", 2, 3, 1, 2)]
    [InlineData("180", 3, 2, 0, 1)]
    [InlineData("270", 2, 3, 0, 0)]
    public void Fit_EachExplicitRotation_PlacesModulesWhereTheFirmwareDoes(
        string rotation, int expectedWidth, int expectedHeight, int expectedX, int expectedY)
    {
        var layout = ScreenFitLayout.Fit(CornerMark2D(), quiet: 0, minModule: 1, rotation: rotation, canvasWidth: 60, canvasHeight: 60);

        Assert.Equal(expectedWidth, layout.Matrix.Width);
        Assert.Equal(expectedHeight, layout.Matrix.Height);
        Assert.Equal((expectedX, expectedY), SoleSetModule(layout.Matrix));
    }

    [Theory]
    // The same three formulas applied to a linear symbology's literally-one-row matrix reproduce the
    // firmware's four linear fillRect cases (BarcodeApplication.cpp, linear loop): Deg0 keeps bar 0
    // leftmost, Deg180 mirrors it to the right end, Deg90 turns the row into a column with bar 0 at
    // the top, Deg270 into the same column with bar 0 at the bottom.
    [InlineData("0", 4, 1, 0, 0)]
    [InlineData("90", 1, 4, 0, 0)]
    [InlineData("180", 4, 1, 3, 0)]
    [InlineData("270", 1, 4, 0, 3)]
    public void Fit_EachExplicitRotation_PlacesLinearBarsWhereTheFirmwareDoes(
        string rotation, int expectedWidth, int expectedHeight, int expectedX, int expectedY)
    {
        var bars = new RawMatrix(4, 1);
        bars[0, 0] = true; // the leftmost bar only

        var layout = ScreenFitLayout.Fit(bars, quiet: 0, minModule: 1, rotation: rotation, canvasWidth: 200, canvasHeight: 200);

        Assert.Equal(ScreenFitLayout.LinearBarModules, layout.BarModules);
        Assert.Equal(expectedWidth, layout.Matrix.Width);
        Assert.Equal(expectedHeight, layout.Matrix.Height);
        Assert.Equal((expectedX, expectedY), SoleSetModule(layout.Matrix));
    }

    [Fact]
    public void Fit_AllFourExplicitRotations_ProduceDistinctModuleLayouts()
    {
        // The SquareMatrix checkerboard is invariant under all four rotations, so distinctness has to
        // be claimed against a genuinely asymmetric symbol - a block in one corner only, like a QR
        // finder pattern.
        var asymmetric = new RawMatrix(21, 21);
        for (var y = 0; y < 21; y++)
            for (var x = 0; x < 21; x++)
                asymmetric[x, y] = x < 5 && y < 3;

        var packed = new[] { "0", "90", "180", "270" }
            .Select(r => Convert.ToBase64String(
                ScreenFitLayout.Fit(asymmetric, quiet: 4, minModule: 1, rotation: r, canvasWidth: 480, canvasHeight: 480).Matrix.Pack()))
            .ToArray();

        Assert.Equal(4, packed.Distinct().Count());
    }

    [Theory]
    [InlineData("0")]
    [InlineData("90")]
    [InlineData("180")]
    [InlineData("270")]
    [InlineData("auto")]
    public void Fit_RotatedSymbol_StillDecodes(string rotation)
    {
        // A rotation that scrambled modules instead of moving them would still produce "distinct"
        // output; decoding the laid-out matrix is what proves the symbol survived the transform.
        var matrix = BarcodeGenerator.Encode(new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" });
        var layout = ScreenFitLayout.Fit(matrix, quiet: 4, minModule: 1, rotation: rotation, canvasWidth: 480, canvasHeight: 480);

        var result = new QRCodeReader().decode(TestDecodeHelpers.ToBinaryBitmap(layout.Matrix));

        Assert.NotNull(result);
        Assert.Equal("LAB-TEST-001", result!.Text);
    }

    [Fact]
    public void Fit_Auto_ResolvesToTheFirmwaresDeg90NotDeg270()
    {
        // Auto weighs Deg0 against Deg90 (calculateLayout), so when the rotated candidate wins the
        // result must be the Deg90 placement, not Deg270's.
        var deg90 = ScreenFitLayout.Fit(CornerMark2D(), quiet: 0, minModule: 1, rotation: "90", canvasWidth: 12, canvasHeight: 60);
        var auto = ScreenFitLayout.Fit(CornerMark2D(), quiet: 0, minModule: 1, rotation: "auto", canvasWidth: 12, canvasHeight: 60);

        // 3x2 in a 12x60 canvas: scale0 = min(4, 30) = 4, scale90 = min(6, 20) = 6 -> rotated wins.
        Assert.Equal(6, auto.Scale);
        Assert.Equal(SoleSetModule(deg90.Matrix), SoleSetModule(auto.Matrix));
    }
}
