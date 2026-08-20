namespace EspBarcode.Generator.Tests;

public class RawMatrixTests
{
    [Fact]
    public void Pack_MatchesDocumentedThreeByTwoExample()
    {
        // docs/PROTOCOL.md "Packing contract": rows "101" then "010" pack to 0xA8.
        var matrix = new RawMatrix(3, 2);
        matrix[0, 0] = true;
        matrix[1, 0] = false;
        matrix[2, 0] = true;
        matrix[0, 1] = false;
        matrix[1, 1] = true;
        matrix[2, 1] = false;

        var packed = matrix.Pack();

        Assert.Single(packed);
        Assert.Equal(0xA8, packed[0]);
    }

    [Fact]
    public void PackAndUnpack_RoundTripsArbitraryPattern()
    {
        var original = new RawMatrix(29, 17);
        for (var y = 0; y < original.Height; y++)
        {
            for (var x = 0; x < original.Width; x++)
            {
                original[x, y] = (x * 7 + y * 3) % 5 == 0;
            }
        }

        var restored = RawMatrix.Unpack(original.Width, original.Height, original.Pack());

        for (var y = 0; y < original.Height; y++)
        {
            for (var x = 0; x < original.Width; x++)
            {
                Assert.Equal(original[x, y], restored[x, y]);
            }
        }
    }

    [Fact]
    public void Pack_LeavesUnusedTrailingBitsZero()
    {
        var matrix = new RawMatrix(3, 1); // 3 bits -> 1 byte, 5 unused low bits
        matrix[0, 0] = true;
        matrix[1, 0] = true;
        matrix[2, 0] = true;

        var packed = matrix.Pack();

        Assert.Single(packed);
        Assert.Equal(0b1110_0000, packed[0]);
    }

    [Fact]
    public void FromGrid_TransposesRowMajorGridIntoXyIndexing()
    {
        bool[,] grid =
        {
            { true, false, true },
            { false, true, false },
        };

        var matrix = RawMatrix.FromGrid(grid);

        Assert.Equal(3, matrix.Width);
        Assert.Equal(2, matrix.Height);
        Assert.True(matrix[0, 0]);
        Assert.False(matrix[1, 0]);
        Assert.True(matrix[2, 0]);
        Assert.False(matrix[0, 1]);
        Assert.True(matrix[1, 1]);
        Assert.False(matrix[2, 1]);
    }
}
