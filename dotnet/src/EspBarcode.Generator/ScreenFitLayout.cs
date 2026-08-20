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

    public static RenderedLayout Fit(RawMatrix matrix, int quiet, int minModule, string rotation, int canvasWidth, int canvasHeight)
    {
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

        bool rotate;
        int scale;
        switch (rotation)
        {
            case "0" or "180":
                rotate = false;
                scale = scale0;
                break;
            case "90" or "270":
                rotate = true;
                scale = scale90;
                break;
            case "auto":
                rotate = scale90 > scale0;
                scale = rotate ? scale90 : scale0;
                break;
            default:
                throw new BarcodeGenerationException("invalid_option", $"Unknown rotation '{rotation}'.");
        }

        if (scale < minModule)
        {
            throw new BarcodeGenerationException(
                "too_dense",
                $"Symbol ({contentWidth}x{contentHeight} modules, quiet {quiet}) does not fit a {canvasWidth}x{canvasHeight} canvas at min_module {minModule}.");
        }

        // Centering has to use the same substituted extent the scale was chosen against, or a
        // 48-module-long bar gets centred as though it were one module and runs off the canvas.
        var laidOutMatrix = rotate ? Rotate90(matrix) : matrix;
        var logicalWidth = (rotate ? contentHeight : contentWidth) + 2 * quiet;
        var logicalHeight = (rotate ? contentWidth : contentHeight) + 2 * quiet;
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

    private static RawMatrix Rotate90(RawMatrix matrix)
    {
        var rotated = new RawMatrix(matrix.Height, matrix.Width);
        for (var y = 0; y < matrix.Height; y++)
            for (var x = 0; x < matrix.Width; x++)
                rotated[y, matrix.Width - 1 - x] = matrix[x, y];
        return rotated;
    }
}
