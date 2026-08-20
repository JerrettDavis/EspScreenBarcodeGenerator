using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

public enum OpenMode { None, System, Viewer }

public sealed record ParsedGenerateCommand(BarcodeSpec Spec, string? OutPath, OpenMode Open);

internal static class SpecArgs
{
    public static ParsedGenerateCommand Parse(string[] args)
    {
        if (args.Length < 2 || args[0] != "generate")
            throw new BarcodeGenerationException("invalid_args", "Usage: espbarcode-viewer generate <type> <data|@file> [options]");

        BarcodeType type;
        try
        {
            type = BarcodeTypeExtensions.ParseWireValue(args[1]);
        }
        catch (BarcodeGenerationException ex)
        {
            throw new BarcodeGenerationException("invalid_args", ex.Message);
        }

        if (args.Length < 3)
            throw new BarcodeGenerationException("invalid_args", "Missing <data|@file> argument.");

        var data = PayloadSource.Resolve(args[2]);
        var rest = args[3..];

        var outPath = ExtractOption(rest, "--out");
        var open = ExtractOption(rest, "--open") switch
        {
            null or "none" => OpenMode.None,
            "system" => OpenMode.System,
            "viewer" => OpenMode.Viewer,
            var other => throw new BarcodeGenerationException("invalid_args", $"Unknown --open mode '{other}'."),
        };

        var spec = new BarcodeSpec
        {
            Type = type,
            Data = data,
            Ecc = ExtractOption(rest, "--ecc") ?? "M",
            Rotation = ExtractOption(rest, "--rotation") ?? "auto",
            Quiet = int.Parse(ExtractOption(rest, "--quiet") ?? "-1"),
            MinModule = int.Parse(ExtractOption(rest, "--min-module") ?? "2"),
            Rectangular = HasFlag(rest, "--rect"),
            Invert = HasFlag(rest, "--invert"),
            Checksum = !HasFlag(rest, "--no-checksum"),
            QrMinVersion = int.Parse(ExtractOption(rest, "--qr-min-version") ?? "1"),
            QrMaxVersion = int.Parse(ExtractOption(rest, "--qr-max-version") ?? "20"),
            AztecSecurity = int.Parse(ExtractOption(rest, "--aztec-security") ?? "23"),
            AztecLayers = int.Parse(ExtractOption(rest, "--aztec-layers") ?? "1"),
        };

        return new ParsedGenerateCommand(spec, outPath, open);
    }

    private static string? ExtractOption(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == name) return args[i + 1];
        return null;
    }

    private static bool HasFlag(string[] args, string name) => args.Contains(name);
}
