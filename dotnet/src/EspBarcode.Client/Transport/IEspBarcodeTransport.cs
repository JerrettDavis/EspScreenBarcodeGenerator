namespace EspBarcode.Client.Transport;

/// <summary>Line-oriented transport for the NDJSON-over-serial protocol.</summary>
public interface IEspBarcodeTransport : IDisposable
{
    void WriteLine(string line);

    /// <summary>
    /// Waits for the next newline-terminated line, or returns <c>null</c> if none
    /// arrived within the transport's own poll timeout. Callers re-poll against
    /// their own overall request deadline, matching the device's behavior of
    /// emitting unrelated boot-log lines on the same UART.
    /// </summary>
    string? ReadLine();
}
