namespace EspBarcode.Client.TransportV2;

using EspBarcode.Client.Transport;

public static class UpgradeHandshake
{
    /// <summary>
    /// Sends the v1 <c>upgrade</c> request that switches the firmware's UART framing
    /// from NDJSON to EspLink v2 COBS frames (see src/SerialLegacyEndpoint.cpp's
    /// "upgrade" handling). Throws <see cref="EspBarcodeProtocolException"/> if the
    /// firmware rejects the request, and <see cref="TimeoutException"/> if it never answers.
    /// </summary>
    public static void RequestUpgrade(IEspBarcodeTransport transport)
    {
        var client = new EspBarcodeClient(transport);
        client.Request("upgrade");
    }
}
