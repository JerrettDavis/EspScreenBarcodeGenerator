using System.IO.Ports;
using EspBarcode.Client;

namespace EspBarcode.Cli;

internal static class CliApp
{
    public static int Run(string[] args)
    {
        if (args.Length == 0)
        {
            PrintUsage();
            return 1;
        }

        var command = args[0];
        var rest = args[1..];

        if (command is "list-ports")
        {
            foreach (var name in SerialPort.GetPortNames()) Console.WriteLine(name);
            return 0;
        }

        if (command is "-h" or "--help" or "help")
        {
            PrintUsage();
            return 0;
        }

        var portName = ExtractOption(rest, "--port") ?? Environment.GetEnvironmentVariable("ESP_BARCODE_PORT");
        if (portName is null)
        {
            Console.Error.WriteLine("error: no serial port given. Pass --port COM7 (or /dev/ttyUSB0) or set ESP_BARCODE_PORT.");
            return 1;
        }

        try
        {
            using var client = EspBarcodeClient.Connect(portName);
            switch (command)
            {
                case "hello":
                    Scenarios.Hello(client);
                    break;
                case "qr":
                    Scenarios.GenerateQr(client, rest);
                    break;
                case "upc":
                    Scenarios.GenerateUpc(client, rest);
                    break;
                case "code128":
                    Scenarios.GenerateCode128(client, rest);
                    break;
                case "license":
                    Scenarios.GenerateLicensePdf417(client, rest);
                    break;
                case "demo":
                    Scenarios.RunAll(client);
                    break;
                default:
                    Console.Error.WriteLine($"error: unknown command '{command}'");
                    PrintUsage();
                    return 1;
            }
        }
        catch (EspBarcodeProtocolException ex)
        {
            Console.Error.WriteLine($"device error [{ex.Code}]: {ex.Message}");
            return 1;
        }
        catch (TimeoutException ex)
        {
            Console.Error.WriteLine($"timeout: {ex.Message}");
            return 1;
        }

        return 0;
    }

    private static string? ExtractOption(string[] args, string name)
    {
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == name) return args[i + 1];
        }
        return null;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            EspBarcode.Cli - demo client for the EspScreenBarcodeGenerator USB protocol

            Usage: espbarcode <command> --port <COMx> [options]

            Commands:
              list-ports              List available serial ports
              hello                   Query device identity and protocol version
              qr [--data TEXT]        Generate and display a QR code
              upc [--data DIGITS]     Generate and display a UPC-A barcode
              code128 [--data TEXT]   Generate and display a Code 128 barcode
              license                 Render a synthetic driver's-license-style
                                      PDF417 barcode on the host (via ZXing.Net,
                                      since the device can't generate PDF417
                                      on-board) and upload it through the raw
                                      matrix protocol
              demo                    Run through every scenario above in sequence

            Environment:
              ESP_BARCODE_PORT        Default value for --port
            """);
    }
}
