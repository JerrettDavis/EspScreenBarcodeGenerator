using System.Globalization;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

public enum OpenMode { None, System, Viewer }

public sealed record ParsedGenerateCommand(BarcodeSpec Spec, string? OutPath, OpenMode Open);

internal static class SpecArgs
{
    /// <summary>Options that take the following token as their value.</summary>
    /// <remarks><c>--viewer-port</c>/<c>--viewer-exe</c> are listed even though this class never
    /// reads them: <see cref="ViewerClient"/> pulls them off the same argument array later, so they
    /// are legal on a <c>generate</c> command and neither they nor their values are "unknown".</remarks>
    private static readonly string[] ValueOptions =
    [
        "--out", "--open", "--ecc", "--rotation", "--quiet", "--min-module",
        "--qr-min-version", "--qr-max-version", "--aztec-security", "--aztec-layers",
        "--viewer-port", "--viewer-exe",
    ];

    /// <summary>Options that stand alone and consume no following token.</summary>
    private static readonly string[] FlagOptions = ["--rect", "--invert", "--no-checksum"];

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

        RejectUnrecognizedOptions(rest);

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
            Quiet = ExtractIntOption(rest, "--quiet", -1),
            MinModule = ExtractIntOption(rest, "--min-module", 2),
            Rectangular = HasFlag(rest, "--rect"),
            Invert = HasFlag(rest, "--invert"),
            Checksum = !HasFlag(rest, "--no-checksum"),
            QrMinVersion = ExtractIntOption(rest, "--qr-min-version", 1),
            QrMaxVersion = ExtractIntOption(rest, "--qr-max-version", 20),
            AztecSecurity = ExtractIntOption(rest, "--aztec-security", 23),
            AztecLayers = ExtractIntOption(rest, "--aztec-layers", 1),
        };

        return new ParsedGenerateCommand(spec, outPath, open);
    }

    /// <summary>
    /// Fails on any <c>--</c>-prefixed token this CLI does not know. A silently ignored
    /// <c>--rotaton 90</c> is a real trap for a tool whose whole job is reproducing an exact
    /// parameter set for a scanner test: the run would exit 0 having produced a symbol at the
    /// default rotation instead.
    /// </summary>
    private static void RejectUnrecognizedOptions(string[] args)
    {
        for (var i = 0; i < args.Length; i++)
        {
            var token = args[i];

            if (FlagOptions.Contains(token)) continue;

            if (ValueOptions.Contains(token))
            {
                if (i + 1 >= args.Length)
                    throw new BarcodeGenerationException("invalid_args", $"Option '{token}' requires a value.");
                i++; // its value is not an option, however it is spelled
                continue;
            }

            if (token.StartsWith("--", StringComparison.Ordinal))
                throw new BarcodeGenerationException("invalid_args", $"Unknown option '{token}'.");

            // A bare token here is a stray positional (the type and payload were already consumed).
            // Leaving it alone preserves the pre-existing tolerance; only misspelled *options* were
            // reported as a trap.
        }
    }

    /// <summary>Rejects a non-numeric value for a numeric option as a normal validation error, so a
    /// typo surfaces as "error [invalid_args]: ..." and exit 1 rather than a raw FormatException
    /// stack trace out of int.Parse.</summary>
    private static int ExtractIntOption(string[] args, string name, int fallback)
    {
        var raw = ExtractOption(args, name);
        if (raw is null) return fallback;
        if (!int.TryParse(raw, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var value))
            throw new BarcodeGenerationException("invalid_args", $"Option '{name}' requires an integer, got '{raw}'.");
        return value;
    }

    private static string? ExtractOption(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == name) return args[i + 1];
        return null;
    }

    private static bool HasFlag(string[] args, string name) => args.Contains(name);
}
