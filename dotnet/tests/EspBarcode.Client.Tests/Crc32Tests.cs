using System.Text;

namespace EspBarcode.Client.Tests;

public class Crc32Tests
{
    [Fact]
    public void Compute_MatchesTheOfficialCrc32IsoHdlcCheckValue()
    {
        // The standard CRC-32/ISO-HDLC catalog check value for ASCII "123456789",
        // reproduced by Python's binascii.crc32 (the algorithm docs/PROTOCOL.md pins to).
        var crc = Crc32.Compute(Encoding.ASCII.GetBytes("123456789"));

        Assert.Equal(0xCBF43926u, crc);
    }

    [Fact]
    public void Compute_OfEmptyInputIsZero()
    {
        Assert.Equal(0u, Crc32.Compute([]));
    }

    [Fact]
    public void Compute_OfSingleByteMatchesPython()
    {
        // binascii.crc32(bytes([0xA8])) == 168805463, cross-checked against the
        // 3x2 packing example in docs/PROTOCOL.md.
        Assert.Equal(168805463u, Crc32.Compute([0xA8]));
    }
}
