namespace EspBarcode.Generator;

/// <summary>A matrix laid out for a specific canvas: possibly rotated, with its integer pixel scale and centering offsets. Nothing here is resized/antialiased — see docs/ARCHITECTURE.md "Rendering pipeline", which this reimplements against an arbitrary target canvas instead of the ESP's fixed 320x480 screen.</summary>
public sealed record RenderedLayout(RawMatrix Matrix, int Scale, int QuietModules, int OffsetXPixels, int OffsetYPixels, int CanvasWidth, int CanvasHeight);

public static class ScreenFitLayout
{
    public static int ResolveQuietZone(BarcodeType type, int specQuiet)
    {
        if (specQuiet >= 0) return specQuiet;
        return type is BarcodeType.Qr or BarcodeType.DataMatrix or BarcodeType.Aztec or BarcodeType.Pdf417 ? 4 : 10;
    }

    public static RenderedLayout Fit(RawMatrix matrix, int quiet, int minModule, string rotation, int canvasWidth, int canvasHeight)
    {
        var scale0 = IntegerScale(matrix.Width, matrix.Height, quiet, canvasWidth, canvasHeight);
        var scale90 = IntegerScale(matrix.Height, matrix.Width, quiet, canvasWidth, canvasHeight);

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
                $"Symbol ({matrix.Width}x{matrix.Height} modules, quiet {quiet}) does not fit a {canvasWidth}x{canvasHeight} canvas at min_module {minModule}.");
        }

        var laidOutMatrix = rotate ? Rotate90(matrix) : matrix;
        var logicalWidth = laidOutMatrix.Width + 2 * quiet;
        var logicalHeight = laidOutMatrix.Height + 2 * quiet;
        var offsetX = (canvasWidth - logicalWidth * scale) / 2;
        var offsetY = (canvasHeight - logicalHeight * scale) / 2;

        return new RenderedLayout(laidOutMatrix, scale, quiet, offsetX, offsetY, canvasWidth, canvasHeight);
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
