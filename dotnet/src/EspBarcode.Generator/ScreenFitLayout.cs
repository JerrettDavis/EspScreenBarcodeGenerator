namespace EspBarcode.Generator;

/// <summary>A matrix laid out for a specific canvas: possibly rotated, with its integer pixel scale and centering offsets. Nothing here is resized/antialiased — see docs/ARCHITECTURE.md "Rendering pipeline", which this reimplements against an arbitrary target canvas instead of the ESP's fixed 320x480 screen.</summary>
/// <param name="BarModules">For a linear symbology, how many modules each bar spans along its long
/// axis; 0 for a 2D symbol, whose extent in both axes comes from <paramref name="Matrix"/> itself.
/// <see cref="ScreenFitLayout.Fit"/> resolves this once so <c>BarcodeImageRenderer</c> never has to
/// re-derive "is this linear" from the matrix shape.</param>
public sealed record RenderedLayout(RawMatrix Matrix, int Scale, int QuietModules, int OffsetXPixels, int OffsetYPixels, int CanvasWidth, int CanvasHeight, int BarModules = 0);

public static class ScreenFitLayout
{
    /// <summary>How many modules long a linear symbol's bars are drawn, matching the firmware's
    /// <c>contentH = linear ? 48U : matrix.height()</c> (lib/EspBarcodeCore/src/EspBarcodeCore.cpp,
    /// <c>candidate()</c>). ZXing's one-dimensional writers return a single row of bars carrying no
    /// height of their own, so without this substitution a Code 128 symbol is both laid out and
    /// drawn one module tall — an unscannable hairline.</summary>
    public const int LinearBarModules = 48;

    public static int ResolveQuietZone(BarcodeType type, int specQuiet)
    {
        if (specQuiet >= 0) return specQuiet;
        return type is BarcodeType.Qr or BarcodeType.DataMatrix or BarcodeType.Aztec or BarcodeType.Pdf417 ? 4 : 10;
    }

    /// <summary>The four concrete orientations the firmware draws (its <c>Rotation::Auto</c> is
    /// resolved to one of these before anything is placed).</summary>
    private enum Orientation { Deg0, Deg90, Deg180, Deg270 }

    public static RenderedLayout Fit(RawMatrix matrix, int quiet, int minModule, string rotation, int canvasWidth, int canvasHeight)
    {
        // The firmware clamps the requested minimum before scoring any candidate
        // (lib/EspBarcodeCore/src/EspBarcodeCore.cpp, calculateLayout:
        // `minimumModulePixels = std::max<uint8_t>(minimumModulePixels, 1)`). Without the clamp,
        // min_module 0 makes the too-dense guard below vacuous, and a symbol wider than the canvas
        // lands on an integer scale of 0 — every module then draws as a zero-sized rectangle and the
        // caller gets a completely blank PNG with a success exit code.
        minModule = Math.Max(minModule, 1);

        // Substitute the conceptual bar length for a linear symbol's height *before* the orientation
        // choice, exactly as the firmware does. Doing it here rather than at draw time is what keeps
        // auto-rotation honest: scored against a literal height of 1, the rotated candidate always
        // looks dramatically better than it really is, so every linear symbol gets rotated and
        // scaled as if it were a thread. A height of 1 is a safe detector — the smallest supported
        // 2D symbols are 21x21 (QR), ~10x10 (Data Matrix) and ~15x15 (Aztec).
        var linear = matrix.Height == 1;
        var contentWidth = matrix.Width;
        var contentHeight = linear ? LinearBarModules : matrix.Height;

        var scale0 = IntegerScale(contentWidth, contentHeight, quiet, canvasWidth, canvasHeight);
        var scale90 = IntegerScale(contentHeight, contentWidth, quiet, canvasWidth, canvasHeight);

        Orientation orientation;
        int scale;
        switch (rotation)
        {
            case "0":
                orientation = Orientation.Deg0;
                scale = scale0;
                break;
            case "180":
                orientation = Orientation.Deg180;
                scale = scale0;
                break;
            case "90":
                orientation = Orientation.Deg90;
                scale = scale90;
                break;
            case "270":
                orientation = Orientation.Deg270;
                scale = scale90;
                break;
            case "auto":
                // The firmware's auto weighs Deg0 against Deg90 only, and takes Deg90 strictly on a
                // larger module size (calculateLayout).
                var preferRotated = scale90 > scale0;
                orientation = preferRotated ? Orientation.Deg90 : Orientation.Deg0;
                scale = preferRotated ? scale90 : scale0;
                break;
            default:
                throw new BarcodeGenerationException("invalid_option", $"Unknown rotation '{rotation}'.");
        }

        // `scale == 0` is redundant while minModule is clamped to >= 1 (a non-negative integer scale
        // below 1 is exactly 0), but it is the firmware's own guard verbatim — `scale < minimum ||
        // scale == 0` in candidate() — and keeps the blank-image case impossible if the clamp above
        // is ever relaxed.
        if (scale < minModule || scale == 0)
        {
            throw new BarcodeGenerationException(
                "too_dense",
                $"Symbol ({contentWidth}x{contentHeight} modules, quiet {quiet}) does not fit a {canvasWidth}x{canvasHeight} canvas at min_module {minModule}.");
        }

        // Centering has to use the same substituted extent the scale was chosen against, or a
        // 48-module-long bar gets centred as though it were one module and runs off the canvas.
        var swapsAxes = orientation is Orientation.Deg90 or Orientation.Deg270;
        var laidOutMatrix = Rotate(matrix, orientation);
        var logicalWidth = (swapsAxes ? contentHeight : contentWidth) + 2 * quiet;
        var logicalHeight = (swapsAxes ? contentWidth : contentHeight) + 2 * quiet;
        var offsetX = (canvasWidth - logicalWidth * scale) / 2;
        var offsetY = (canvasHeight - logicalHeight * scale) / 2;

        return new RenderedLayout(laidOutMatrix, scale, quiet, offsetX, offsetY, canvasWidth, canvasHeight, linear ? LinearBarModules : 0);
    }

    private static int IntegerScale(int width, int height, int quiet, int canvasWidth, int canvasHeight)
    {
        var logicalWidth = width + 2 * quiet;
        var logicalHeight = height + 2 * quiet;
        return Math.Min(canvasWidth / logicalWidth, canvasHeight / logicalHeight);
    }

    /// <summary>
    /// Bakes the chosen orientation into the matrix, using the firmware's own per-module formulas
    /// (src/BarcodeApplication.cpp, <c>renderCurrent</c>'s 2D loop):
    /// <code>
    /// Deg90:  rx = height - 1 - moduleY; ry = moduleX
    /// Deg180: rx = width  - 1 - moduleX; ry = height - 1 - moduleY
    /// Deg270: rx = moduleY;              ry = width  - 1 - moduleX
    /// </code>
    /// The same three formulas reproduce the firmware's *linear* placements too, because a linear
    /// symbol's matrix is literally one row tall: Deg90/Deg270 turn it into a one-column matrix whose
    /// row index is the bar index (ascending, then descending), and Deg180 leaves it one row tall
    /// with the bars mirrored — which is exactly what <c>BarcodeApplication.cpp</c>'s four
    /// <c>fillRect</c> cases place, and exactly what <c>BarcodeImageRenderer.DrawLinearBars</c>
    /// expects to find.
    /// </summary>
    private static RawMatrix Rotate(RawMatrix matrix, Orientation orientation)
    {
        if (orientation == Orientation.Deg0) return matrix;

        var swapsAxes = orientation is Orientation.Deg90 or Orientation.Deg270;
        var rotated = new RawMatrix(
            swapsAxes ? matrix.Height : matrix.Width,
            swapsAxes ? matrix.Width : matrix.Height);

        for (var y = 0; y < matrix.Height; y++)
        {
            for (var x = 0; x < matrix.Width; x++)
            {
                var (rx, ry) = orientation switch
                {
                    Orientation.Deg90 => (matrix.Height - 1 - y, x),
                    Orientation.Deg180 => (matrix.Width - 1 - x, matrix.Height - 1 - y),
                    _ => (y, matrix.Width - 1 - x), // Deg270
                };
                rotated[rx, ry] = matrix[x, y];
            }
        }

        return rotated;
    }
}
