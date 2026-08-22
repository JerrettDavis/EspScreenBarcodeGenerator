using EspBarcode.Client.Transport;
using EspBarcode.Connectivity.Client;

namespace EspBarcode.Client.TransportV2;

/// <summary>
/// Connects to a board acting as the USB&lt;-&gt;ESP-NOW gateway (see GatewayHandshake), then
/// hands back the same byte-stream <see cref="ILinkConnection"/> <see cref="SerialV2Connector"/>
/// uses -- the wire framing is identical EspLink v2 COBS hop frames either way; only the far end
/// of those frames differs (this board's own command dispatch vs. an ESP-NOW-relayed display).
/// </summary>
public sealed class GatewaySerialConnector(string portName, int baudRate = 115200) : ILinkConnector
{
    public Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken)
    {
        var v1Transport = new SerialPortTransport(portName, baudRate);
        GatewayHandshake.RequestGatewayMode(v1Transport);
        return Task.FromResult<ILinkConnection>(new SerialLinkConnection(v1Transport.UnderlyingPort));
    }
}
