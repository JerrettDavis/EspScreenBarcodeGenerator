namespace EspBarcode.Generator;

public static class PayloadSource
{
    public static string Resolve(string argument)
    {
        if (!argument.StartsWith('@')) return argument;

        var path = argument[1..];
        if (!File.Exists(path))
            throw new BarcodeGenerationException("payload_file_not_found", $"Payload file not found: '{path}'.");

        return File.ReadAllText(path).TrimEnd('\r', '\n');
    }
}
