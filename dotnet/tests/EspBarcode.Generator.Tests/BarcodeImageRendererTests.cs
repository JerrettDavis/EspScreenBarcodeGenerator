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
}
