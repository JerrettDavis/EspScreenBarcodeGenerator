using System.Text;

namespace EspBarcode.Generator;

/// <summary>Check-digit computation/validation and FNC1 token handling shared by the retail (UPC/EAN/ITF-14/MSI) and Code128/GS1-128 encoders.</summary>
internal static class Checksums
{
    public static int Gs1CheckDigit(string digitsWithoutCheck)
    {
        var sum = 0;
        for (var i = 0; i < digitsWithoutCheck.Length; i++)
        {
            var digit = digitsWithoutCheck[digitsWithoutCheck.Length - 1 - i] - '0';
            var weight = i % 2 == 0 ? 3 : 1;
            sum += digit * weight;
        }
        return (10 - sum % 10) % 10;
    }

    public static int LuhnCheckDigit(string digits)
    {
        var sum = 0;
        var doubleIt = true;
        for (var i = digits.Length - 1; i >= 0; i--)
        {
            var d = digits[i] - '0';
            if (doubleIt)
            {
                d *= 2;
                if (d > 9) d -= 9;
            }
            sum += d;
            doubleIt = !doubleIt;
        }
        return (10 - sum % 10) % 10;
    }

    private static void RequireDigits(string data, string what)
    {
        if (data.Length == 0 || !data.All(char.IsAsciiDigit))
            throw new BarcodeGenerationException("invalid_payload", $"{what} requires digits only, got '{data}'.");
    }

    private static string NormalizeGs1Family(string data, int shortLength, int fullLength, bool checksum, string what)
    {
        RequireDigits(data, what);
        if (data.Length == shortLength)
        {
            return data + Gs1CheckDigit(data);
        }
        if (data.Length == fullLength)
        {
            if (!checksum) return data;
            var body = data[..shortLength];
            var expected = Gs1CheckDigit(body);
            if (data[^1] - '0' != expected)
                throw new BarcodeGenerationException("invalid_checksum", $"{what} check digit mismatch: '{data}' expected digit {expected}.");
            return data;
        }
        throw new BarcodeGenerationException("invalid_payload", $"{what} requires {shortLength} or {fullLength} digits, got {data.Length}.");
    }

    public static string NormalizeUpcA(string data, bool checksum) => NormalizeGs1Family(data, 11, 12, checksum, "UPC-A");
    public static string NormalizeEan13(string data, bool checksum) => NormalizeGs1Family(data, 12, 13, checksum, "EAN-13");
    public static string NormalizeEan8(string data, bool checksum) => NormalizeGs1Family(data, 7, 8, checksum, "EAN-8");
    public static string NormalizeItf14(string data, bool checksum) => NormalizeGs1Family(data, 13, 14, checksum, "ITF-14");

    public static string NormalizeItf(string data)
    {
        RequireDigits(data, "ITF");
        return data.Length % 2 == 0 ? data : "0" + data;
    }

    public static string NormalizeMsi(string data, bool checksum)
    {
        RequireDigits(data, "MSI");
        return checksum ? data + LuhnCheckDigit(data) : data;
    }

    public static string NormalizeCodabar(string data)
    {
        if (data.Length == 0) throw new BarcodeGenerationException("invalid_payload", "Codabar payload must not be empty.");
        var startOk = "ABCDabcd".IndexOf(data[0]) >= 0;
        var stopOk = "ABCDabcd".IndexOf(data[^1]) >= 0;
        if (startOk && stopOk) return data;
        return "A" + data + "A";
    }

    public static string NormalizeFnc1Tokens(string data)
    {
        var sb = new StringBuilder(data);
        sb.Replace("{FNC1}", "ñ").Replace("<FNC1>", "ñ").Replace("<GS>", "ñ").Replace("" + (char)0x1D, "ñ");
        return sb.ToString();
    }
}