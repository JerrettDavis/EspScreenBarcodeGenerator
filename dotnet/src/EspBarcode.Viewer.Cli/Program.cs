using EspBarcode.Viewer.Cli;

try
{
    return CliApp.Run(args);
}
catch (Exception ex)
{
    // Last-resort backstop. Everything expected is already reported through
    // CliApp.Run's BarcodeGenerationException handler; this only exists so that a
    // path nobody anticipated still leaves the user with one readable line and
    // exit 1 instead of a raw .NET stack trace.
    Console.Error.WriteLine($"error: {ex.Message.ReplaceLineEndings(" ").Trim()}");
    return 1;
}
