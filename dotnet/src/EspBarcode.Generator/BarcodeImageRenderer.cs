using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.Versioning;

namespace EspBarcode.Generator;

[SupportedOSPlatform("windows")]
public static class BarcodeImageRenderer
{
    public static byte[] Render(RenderedLayout layout, bool invert)
    {
        var background = invert ? Color.Black : Color.White;
        var foreground = invert ? Color.White : Color.Black;

        using var bitmap = new Bitmap(layout.CanvasWidth, layout.CanvasHeight, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(bitmap))
        {
            g.Clear(background);
            using var brush = new SolidBrush(foreground);
            for (var y = 0; y < layout.Matrix.Height; y++)
            {
                for (var x = 0; x < layout.Matrix.Width; x++)
                {
                    if (!layout.Matrix[x, y]) continue;
                    var px = layout.OffsetXPixels + (layout.QuietModules + x) * layout.Scale;
                    var py = layout.OffsetYPixels + (layout.QuietModules + y) * layout.Scale;
                    g.FillRectangle(brush, px, py, layout.Scale, layout.Scale);
                }
            }
        }

        using var output = new MemoryStream();
        bitmap.Save(output, ImageFormat.Png);
        return output.ToArray();
    }
}
