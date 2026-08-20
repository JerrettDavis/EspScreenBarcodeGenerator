namespace EspBarcode.Client;

/// <summary>
/// Standard IEEE CRC-32 (polynomial 0xEDB88320, init/final XOR 0xFFFFFFFF) —
/// the exact variant docs/PROTOCOL.md specifies for raw matrix transfers,
/// matching Python's <c>binascii.crc32</c>/<c>zlib.crc32</c>.
/// </summary>
internal static class Crc32
{
    private static readonly uint[] Table = BuildTable();

    private static uint[] BuildTable()
    {
        const uint polynomial = 0xEDB88320u;
        var table = new uint[256];
        for (uint i = 0; i < table.Length; i++)
        {
            var c = i;
            for (var k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? polynomial ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return table;
    }

    public static uint Compute(ReadOnlySpan<byte> data)
    {
        var crc = 0xFFFFFFFFu;
        foreach (var b in data)
        {
            crc = Table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }
}
