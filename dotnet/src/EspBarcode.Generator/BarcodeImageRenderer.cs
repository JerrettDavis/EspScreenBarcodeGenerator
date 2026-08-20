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
            if (layout.BarModules > 0) DrawLinearBars(g, brush, layout);
            else DrawModuleCells(g, brush, layout);
        }

        using var output = new MemoryStream();
        bitmap.Save(output, ImageFormat.Png);
        return output.ToArray();
    }

    /// <summary>Draws a 2D symbol: one filled square per set module.</summary>
    private static void DrawModuleCells(Graphics g, Brush brush, RenderedLayout layout)
    {
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

    /// <summary>
    /// Draws a linear symbol: each set module in the single row of bars becomes one rectangle a
    /// module wide and <see cref="RenderedLayout.BarModules"/> modules long, mirroring the firmware's
    /// two fillRect shapes in src/BarcodeApplication.cpp. The per-cell square loop would otherwise
    /// paint the whole symbol one module tall, because that is literally all the matrix contains.
    /// </summary>
    private static void DrawLinearBars(Graphics g, Brush brush, RenderedLayout layout)
    {
        var scale = layout.Scale;
        var quietPixels = layout.QuietModules * scale;
        var barLength = layout.BarModules * scale;

        if (layout.Matrix.Height == 1)
        {
            // Unrotated: the row of bars runs across the canvas, each bar running down it.
            for (var x = 0; x < layout.Matrix.Width; x++)
            {
                if (!layout.Matrix[x, 0]) continue;
                g.FillRectangle(brush,
                    layout.OffsetXPixels + (layout.QuietModules + x) * scale,
                    layout.OffsetYPixels + quietPixels,
                    scale,
                    barLength);
            }
        }
        else
        {
            // Rotated: ScreenFitLayout turned the Nx1 row into a 1xN column, so the bars now run
            // across the canvas and the row of them runs down it.
            for (var y = 0; y < layout.Matrix.Height; y++)
            {
                if (!layout.Matrix[0, y]) continue;
                g.FillRectangle(brush,
                    layout.OffsetXPixels + quietPixels,
                    layout.OffsetYPixels + (layout.QuietModules + y) * scale,
                    barLength,
                    scale);
            }
        }
    }
}
