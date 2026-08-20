using EspBarcode.Client;
using ZXing;
using ZXing.PDF417;

namespace EspBarcode.Cli;

/// <summary>Common test scenarios against a connected device, exercised by CliApp and runnable together via "demo".</summary>
internal static class Scenarios
{
    public static void Hello(EspBarcodeClient client)
    {
        var info = client.Hello();
        Console.WriteLine($"{info.Device} firmware {info.Firmware} (protocol {info.Protocol}), screen {info.ScreenWidth}x{info.ScreenHeight}, transport {info.Transport}");
    }

    public static void GenerateQr(EspBarcodeClient client, string[] args)
    {
        var data = ExtractOption(args, "--data") ?? "LAB-TEST-001";
        var result = client.Generate(new GenerateOptions { Type = BarcodeType.Qr, Data = data });
        Report("QR", result);
    }

    public static void GenerateUpc(EspBarcodeClient client, string[] args)
    {
        // 11 digits; the device computes and appends the UPC-A check digit.
        var data = ExtractOption(args, "--data") ?? "03600029145";
        var result = client.Generate(new GenerateOptions { Type = BarcodeType.UpcA, Data = data });
        Report("UPC-A", result);
    }

    public static void GenerateCode128(EspBarcodeClient client, string[] args)
    {
        var data = ExtractOption(args, "--data") ?? "LOT-2026-00042";
        var result = client.Generate(new GenerateOptions { Type = BarcodeType.Code128, Data = data, SaveAs = "LOT_SAMPLE" });
        Report("Code 128", result);
    }

    /// <summary>
    /// The device's on-board encoders don't cover PDF417 (README, "Current
    /// encoder limits"), so this demonstrates the protocol's documented
    /// workaround instead: render PDF417 on the host with ZXing.Net and push
    /// the resulting module matrix through the raw-matrix upload path.
    /// </summary>
    public static void GenerateLicensePdf417(EspBarcodeClient client, string[] args)
    {
        var payload = ExtractOption(args, "--payload") ?? BuildSyntheticLicensePayload();
        var matrix = EncodePdf417(payload);

        var result = client.UploadRawMatrix(matrix, new RawMatrixOptions
        {
            Label = "Synthetic DL (PDF417)",
            Quiet = 4,
        });

        Console.WriteLine($"license (PDF417, host-rendered {matrix.Width}x{matrix.Height} modules) -> uploaded, crc32=0x{result.Crc32:X8}, displayed={result.Displayed}");
    }

    public static void RunAll(EspBarcodeClient client)
    {
        Hello(client);
        GenerateQr(client, []);
        GenerateUpc(client, []);
        GenerateCode128(client, []);
        GenerateLicensePdf417(client, []);
        client.Home();
        Console.WriteLine("demo complete");
    }

    internal static RawMatrix EncodePdf417(string payload)
    {
        var writer = new PDF417Writer();
        var hints = new Dictionary<EncodeHintType, object> { [EncodeHintType.MARGIN] = 0 };
        var bitMatrix = writer.encode(payload, BarcodeFormat.PDF_417, 0, 0, hints);

        var matrix = new RawMatrix(bitMatrix.Width, bitMatrix.Height);
        for (var y = 0; y < bitMatrix.Height; y++)
        {
            for (var x = 0; x < bitMatrix.Width; x++)
            {
                matrix[x, y] = bitMatrix[x, y];
            }
        }
        return matrix;
    }

    /// <summary>
    /// A compact, clearly-synthetic subset of AAMVA DL/ID subfields (DAQ license
    /// number, DCS/DAC name, DBB/DBA dates, DBC sex) — enough to exercise a
    /// realistic driver's-license PDF417 payload shape. Not standards-complete
    /// and not derived from any real document.
    /// </summary>
    internal static string BuildSyntheticLicensePayload()
    {
        string[] fields =
        [
            "DAQD1234567890123",
            "DCSDOE",
            "DACJANE",
            "DBB01011990",
            "DBA01012030",
            "DBC2",
        ];
        return "@\n\rANSI 636000090002DL00410267ZD03050031DL" + string.Concat(fields.Select(f => f + "\n"));
    }

    private static void Report(string label, GenerateResult result)
        => Console.WriteLine($"{label}: {result.Width}x{result.Height} modules, normalized=\"{result.NormalizedData}\", displayed={result.Displayed}");

    private static string? ExtractOption(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == name) return args[i + 1];
        }
        return null;
    }
}
