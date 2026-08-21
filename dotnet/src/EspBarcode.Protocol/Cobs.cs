namespace EspBarcode.Protocol;

public static class Cobs
{
    public static byte[] Encode(ReadOnlySpan<byte> data)
    {
        var output = new List<byte>(data.Length + data.Length / 254 + 2);
        int codeIndex = 0;
        output.Add(0); // placeholder for the first code byte, backpatched below
        byte code = 1;
        for (int i = 0; i < data.Length; i++)
        {
            if (data[i] == 0x00)
            {
                output[codeIndex] = code;
                codeIndex = output.Count;
                output.Add(0); // placeholder for the next code byte
                code = 1;
            }
            else
            {
                output.Add(data[i]);
                code++;
                if (code == 0xFF)
                {
                    output[codeIndex] = code;
                    codeIndex = output.Count;
                    output.Add(0); // placeholder
                    code = 1;
                }
            }
        }
        output[codeIndex] = code; // finalize the last (possibly empty) group unconditionally
        return [.. output];
    }

    public static bool TryDecode(ReadOnlySpan<byte> data, out byte[] decoded)
    {
        var output = new List<byte>(data.Length);
        int read = 0;
        while (read < data.Length)
        {
            byte code = data[read];
            if (code == 0) { decoded = []; return false; }
            read++;
            int blockLen = code - 1;
            if (read + blockLen > data.Length) { decoded = []; return false; }
            for (int i = 0; i < blockLen; i++) output.Add(data[read + i]);
            read += blockLen;
            if (code != 255 && read < data.Length) output.Add(0x00);
        }
        decoded = [.. output];
        return true;
    }
}
