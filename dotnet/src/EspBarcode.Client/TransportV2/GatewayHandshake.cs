namespace EspBarcode.Client.TransportV2;

using EspBarcode.Client.Transport;

public static class GatewayHandshake
{
    /// <summary>
    /// Sends the v1 <c>gateway</c> request that switches a board's UART framing from NDJSON
    /// into a pure USB&lt;-&gt;ESP-NOW relay (see src/SerialLegacyEndpoint.cpp's "gateway"
    /// handling and src/GatewayRelay.cpp). Like <see cref="UpgradeHandshake"/>, the wire
    /// framing afterward is identical EspLink v2 COBS hop frames -- only the far end of those
    /// frames is now the ESP-NOW-connected display board, not this board's own command
    /// dispatch. Throws <see cref="EspBarcodeProtocolException"/> if the firmware rejects the
    /// request, and <see cref="TimeoutException"/> if it never answers.
    /// </summary>
    public static void RequestGatewayMode(IEspBarcodeTransport transport)
    {
        var client = new EspBarcodeClient(transport);
        client.Request("gateway");
    }
}
