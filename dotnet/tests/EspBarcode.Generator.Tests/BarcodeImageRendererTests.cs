using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.Versioning;

namespace EspBarcode.Generator.Tests;

// This test class directly exercises System.Drawing APIs (Bitmap, GetPixel, ImageFormat) to
// assert pixel-exact output, so it carries the same class-level Windows-only marker as the
// production BarcodeImageRenderer it tests. This mirrors the plan's fix (see Task 1 review) of
// scoping [SupportedOSPlatform("windows")] to exactly the types that need it instead of an
// assembly-wide attribute that would cascade CA1416 into every other test class in this project.
[SupportedOSPlatform("windows")]
public class BarcodeImageRendererTests
{
    [Fact]
    public void Render_ProducesPngOfExpectedCanvasSize()
    {
        var matrix = new RawMatrix(2, 2);
        matrix[0, 0] = true;
        matrix[1, 1] = true;
        var layout = new RenderedLayout(matrix, Scale: 10, QuietModules: 1, OffsetXPixels: 0, OffsetYPixels: 0, CanvasWidth: 100, CanvasHeight: 100);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        Assert.Equal(ImageFormat.Png.Guid, bitmap.RawFormat.Guid);
        Assert.Equal(100, bitmap.Width);
        Assert.Equal(100, bitmap.Height);
    }

    [Fact]
    public void Render_DrawnModulePixel_IsBlackOnWhiteByDefault()
    {
        var matrix = new RawMatrix(1, 1);
        matrix[0, 0] = true;
        var layout = new RenderedLayout(matrix, Scale: 10, QuietModules: 0, OffsetXPixels: 0, OffsetYPixels: 0, CanvasWidth: 10, CanvasHeight: 10);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        var center = bitmap.GetPixel(5, 5);
        var corner = bitmap.GetPixel(0, 0); // outside the 1x1 module at scale 10 in a 10x10 canvas -> actually covers whole canvas; assert black
        Assert.Equal(Color.FromArgb(255, 0, 0, 0).ToArgb(), center.ToArgb());
    }

    [Fact]
    public void Render_Invert_SwapsBackgroundAndModuleColors()
    {
        var matrix = new RawMatrix(1, 1);
        matrix[0, 0] = false; // unset module -> shows background color
        var layout = new RenderedLayout(matrix, Scale: 10, QuietModules: 0, OffsetXPixels: 0, OffsetYPixels: 0, CanvasWidth: 10, CanvasHeight: 10);

        var png = BarcodeImageRenderer.Render(layout, invert: true);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        var pixel = bitmap.GetPixel(5, 5);
        Assert.Equal(Color.FromArgb(255, 0, 0, 0).ToArgb(), pixel.ToArgb()); // inverted background is black
    }

    [Fact]
    public void Render_LinearLayout_DrawsEachBarDownTheFullBarLength()
    {
        // One row of bars: set, clear, set. At scale 2 with BarModules 48 each set bar must fill a
        // 2x96 column, not the single 2x2 square the per-module loop would paint.
        var matrix = new RawMatrix(3, 1);
        matrix[0, 0] = true;
        matrix[2, 0] = true;
        var layout = new RenderedLayout(matrix, Scale: 2, QuietModules: 0, OffsetXPixels: 0, OffsetYPixels: 0,
            CanvasWidth: 6, CanvasHeight: 96, BarModules: 48);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        Assert.Equal(Black, bitmap.GetPixel(1, 0).ToArgb());   // top of the first bar
        Assert.Equal(Black, bitmap.GetPixel(1, 95).ToArgb());  // bottom of the first bar, 95 rows down
        Assert.Equal(White, bitmap.GetPixel(3, 50).ToArgb());  // the gap between them stays background
        Assert.Equal(Black, bitmap.GetPixel(5, 50).ToArgb());  // middle of the second bar
    }

    [Fact]
    public void Render_RotatedLinearLayout_DrawsEachBarAcrossTheFullBarLength()
    {
        // After ScreenFitLayout rotates it, the Nx1 row of bars is a 1xN column and each bar runs
        // across the canvas instead of down it.
        var matrix = new RawMatrix(1, 3);
        matrix[0, 0] = true;
        matrix[0, 2] = true;
        var layout = new RenderedLayout(matrix, Scale: 2, QuietModules: 0, OffsetXPixels: 0, OffsetYPixels: 0,
            CanvasWidth: 96, CanvasHeight: 6, BarModules: 48);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        Assert.Equal(Black, bitmap.GetPixel(0, 1).ToArgb());
        Assert.Equal(Black, bitmap.GetPixel(95, 1).ToArgb());
        Assert.Equal(White, bitmap.GetPixel(50, 3).ToArgb());
        Assert.Equal(Black, bitmap.GetPixel(50, 5).ToArgb());
    }

    [Fact]
    public void Render_LinearLayout_QuietZoneOffsetsTheBarsFromTheCanvasEdge()
    {
        var matrix = new RawMatrix(1, 1);
        matrix[0, 0] = true;
        // 1 bar + 1 quiet module each side = 3 logical wide; 48 + 2 = 50 logical tall, at scale 1.
        var layout = new RenderedLayout(matrix, Scale: 1, QuietModules: 1, OffsetXPixels: 0, OffsetYPixels: 0,
            CanvasWidth: 3, CanvasHeight: 50, BarModules: 48);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        Assert.Equal(White, bitmap.GetPixel(1, 0).ToArgb());   // top quiet row
        Assert.Equal(Black, bitmap.GetPixel(1, 1).ToArgb());   // bar starts after the quiet zone
        Assert.Equal(Black, bitmap.GetPixel(1, 48).ToArgb());  // ...and runs all 48 modules
        Assert.Equal(White, bitmap.GetPixel(1, 49).ToArgb());  // bottom quiet row
        Assert.Equal(White, bitmap.GetPixel(0, 25).ToArgb());  // left quiet column
    }

    [Fact]
    public void Render_Code128ThroughTheWholePipeline_ProducesBarsWithRealThickness()
    {
        // End-to-end regression guard for the reported repro
        // (`espbarcode-viewer generate code128 SHIP-9988776655 --out c128.png` came out 2 pixels
        // across). Encode -> Fit -> Render, then measure the drawn symbol's bounding box.
        var spec = new BarcodeSpec { Type = BarcodeType.Code128, Data = "SHIP-9988776655" };
        var matrix = BarcodeGenerator.Encode(spec);
        var quiet = ScreenFitLayout.ResolveQuietZone(spec.Type, spec.Quiet);
        var layout = ScreenFitLayout.Fit(matrix, quiet, spec.MinModule, spec.Rotation, canvasWidth: 320, canvasHeight: 480);

        var png = BarcodeImageRenderer.Render(layout, invert: false);

        using var stream = new MemoryStream(png);
        using var bitmap = new Bitmap(stream);
        var (darkWidth, darkHeight) = DarkExtent(bitmap);

        // The bars' own length is the symbol's shorter axis, and it must be the full 48 modules.
        Assert.Equal(ScreenFitLayout.LinearBarModules * layout.Scale, Math.Min(darkWidth, darkHeight));
        // ...and the row of bars is much longer than that, so neither axis is a hairline.
        Assert.True(Math.Max(darkWidth, darkHeight) > Math.Min(darkWidth, darkHeight),
            $"expected a bar row longer than the bars themselves, got {darkWidth}x{darkHeight}");
    }

    private const int Black = unchecked((int)0xFF000000);
    private const int White = unchecked((int)0xFFFFFFFF);

    /// <summary>Size of the bounding box of all non-background pixels.</summary>
    private static (int Width, int Height) DarkExtent(Bitmap bitmap)
    {
        int minX = bitmap.Width, minY = bitmap.Height, maxX = -1, maxY = -1;
        for (var y = 0; y < bitmap.Height; y++)
        {
            for (var x = 0; x < bitmap.Width; x++)
            {
                if (bitmap.GetPixel(x, y).ToArgb() != Black) continue;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
        return maxX < 0 ? (0, 0) : (maxX - minX + 1, maxY - minY + 1);
    }
}
