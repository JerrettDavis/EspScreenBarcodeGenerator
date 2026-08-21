using System.IO.Ports;
using EspBarcode.Connectivity.Client;

namespace EspBarcode.Client.TransportV2;

public sealed class SerialLinkConnection(SerialPort port) : ILinkConnection
{
    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
        => await port.BaseStream.WriteAsync(bytes, cancellationToken);

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
        => await port.BaseStream.ReadAsync(buffer, cancellationToken);

    public ValueTask DisposeAsync()
    {
        if (port.IsOpen) port.Close();
        port.Dispose();
        return ValueTask.CompletedTask;
    }
}
