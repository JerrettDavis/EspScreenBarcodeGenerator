namespace EspBarcode.Generator;

/// <summary>
/// A logical module matrix, packed MSB-first in continuous row-major order to
/// match the firmware's raw upload/download wire format (docs/PROTOCOL.md,
/// "Packing contract"): module (0,0) is bit 7 of byte 0, rows are not padded
/// to byte boundaries, and unused low bits in the final byte are zero.
/// </summary>
public sealed class RawMatrix
{
    private readonly bool[,] _modules;

    public int Width { get; }
    public int Height { get; }

    public RawMatrix(int width, int height)
    {
        if (width <= 0) throw new ArgumentOutOfRangeException(nameof(width));
        if (height <= 0) throw new ArgumentOutOfRangeException(nameof(height));
        Width = width;
        Height = height;
        _modules = new bool[height, width];
    }

    public bool this[int x, int y]
    {
        get => _modules[y, x];
        set => _modules[y, x] = value;
    }

    /// <summary>Builds a matrix from a row-major <c>[y, x]</c> boolean grid, e.g. a decoded ZXing matrix.</summary>
    public static RawMatrix FromGrid(bool[,] grid)
    {
        var height = grid.GetLength(0);
        var width = grid.GetLength(1);
        var matrix = new RawMatrix(width, height);
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                matrix[x, y] = grid[y, x];
            }
        }
        return matrix;
    }

    public byte[] Pack()
    {
        var totalBits = Width * Height;
        var packed = new byte[(totalBits + 7) / 8];
        var bitIndex = 0;
        for (var y = 0; y < Height; y++)
        {
            for (var x = 0; x < Width; x++)
            {
                if (_modules[y, x])
                {
                    packed[bitIndex / 8] |= (byte)(0x80 >> (bitIndex % 8));
                }
                bitIndex++;
            }
        }
        return packed;
    }

    public static RawMatrix Unpack(int width, int height, ReadOnlySpan<byte> packed)
    {
        var matrix = new RawMatrix(width, height);
        var bitIndex = 0;
        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var byteIndex = bitIndex / 8;
                matrix[x, y] = (packed[byteIndex] & (0x80 >> (bitIndex % 8))) != 0;
                bitIndex++;
            }
        }
        return matrix;
    }
}
