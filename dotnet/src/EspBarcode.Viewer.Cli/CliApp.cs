using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

internal static class CliApp
{
    public static int Run(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
        {
            PrintUsage();
            return args.Length == 0 ? 1 : 0;
        }

        try
        {
            return args[0] switch
            {
                "generate" => RunGenerate(args),
                "close" => ViewerClient.Close(args[1..]),
                _ => Unknown(args[0]),
            };
        }
        catch (BarcodeGenerationException ex)
        {
            Console.Error.WriteLine($"error [{ex.Code}]: {ex.Message}");
            return 1;
        }
    }

    private static int RunGenerate(string[] args)
    {
        var parsed = SpecArgs.Parse(args);

        switch (parsed.Open)
        {
            case OpenMode.Viewer:
                ViewerClient.Render(parsed.Spec, args);
                if (parsed.OutPath is not null) WritePng(parsed.Spec, parsed.OutPath);
                break;

            case OpenMode.System:
            {
                var path = parsed.OutPath ?? Path.Combine(Path.GetTempPath(), $"espbarcode-{Guid.NewGuid():N}.png");
                WritePng(parsed.Spec, path);
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path) { UseShellExecute = true });
                Console.WriteLine(path);
                break;
            }

            case OpenMode.None:
            default:
                if (parsed.OutPath is not null) WritePng(parsed.Spec, parsed.OutPath);
                break;
        }

        return 0;
    }

    private static void WritePng(BarcodeSpec spec, string path)
    {
        // BarcodeImageRenderer.Render is [SupportedOSPlatform("windows")] (System.Drawing.Common
        // GDI+ backing). Guarding with OperatingSystem.IsWindows() satisfies the CA1416 platform-
        // compat analyzer without threading a SupportedOSPlatform attribute up through RunGenerate/
        // Run/Program (which would otherwise also mark the public OpenMode/ParsedGenerateCommand
        // types Windows-only, even though they carry no platform-specific state).
        if (!OperatingSystem.IsWindows())
            throw new BarcodeGenerationException("platform_unsupported", "PNG rendering requires Windows (System.Drawing.Common).");

        var matrix = BarcodeGenerator.Encode(spec);
        var quiet = ScreenFitLayout.ResolveQuietZone(spec.Type, spec.Quiet);
        var layout = ScreenFitLayout.Fit(matrix, quiet, spec.MinModule, spec.Rotation, canvasWidth: 320, canvasHeight: 480);
        var png = BarcodeImageRenderer.Render(layout, spec.Invert);
        File.WriteAllBytes(path, png);
    }

    private static int Unknown(string command)
    {
        Console.Error.WriteLine($"error: unknown command '{command}'");
        PrintUsage();
        return 1;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            espbarcode-viewer - standalone barcode generator/viewer (no ESP required)

            Usage:
              espbarcode-viewer generate <type> <data|@file> [options]
              espbarcode-viewer close [--viewer-port N]

            Options:
              --out PATH               Write a PNG to PATH
              --open none|system|viewer  Open mode (default: none)
              --ecc L|M|Q|H             QR error correction (default: M)
              --rotation auto|0|90|180|270
              --quiet N                 Quiet zone in modules, -1 = symbology default
              --min-module N            Minimum pixels per module (default: 2)
              --rect                    Request rectangular Data Matrix
              --invert                  White modules on black background
              --no-checksum             Disable MSI/retail check-digit computation
              --qr-min-version N / --qr-max-version N
              --aztec-security N / --aztec-layers N
            """);
    }
}
