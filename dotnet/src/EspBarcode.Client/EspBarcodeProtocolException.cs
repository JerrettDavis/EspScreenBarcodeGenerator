namespace EspBarcode.Client;

/// <summary>
/// Thrown when the device answers a request with <c>"ok":false</c>. See
/// docs/PROTOCOL.md "Common error codes" for the full <see cref="Code"/> catalog.
/// </summary>
public sealed class EspBarcodeProtocolException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
