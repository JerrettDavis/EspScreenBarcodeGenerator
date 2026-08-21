using EspBarcode.Client.Transport;
using EspBarcode.Connectivity.Client;

namespace EspBarcode.Client.TransportV2;

public sealed class SerialV2Connector(string portName, int baudRate = 115200) : ILinkConnector
{
    public Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken)
    {
        var v1Transport = new SerialPortTransport(portName, baudRate);
        UpgradeHandshake.RequestUpgrade(v1Transport);  // throws on rejection/timeout; v1Transport stays open either way.
        return Task.FromResult<ILinkConnection>(new SerialLinkConnection(v1Transport.UnderlyingPort));
    }
}
