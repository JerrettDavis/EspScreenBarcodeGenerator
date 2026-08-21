using System.IO.Ports;
using EspBarcode.Connectivity.Client;

namespace EspBarcode.Client.TransportV2;

public sealed class SerialLinkConnection(SerialPort port) : ILinkConnection
{
    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
        => await port.BaseStream.WriteAsync(bytes, cancellationToken);

    public Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        return Task.Run(() =>
        {
            var array = new byte[buffer.Length];
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    int count = port.Read(array, 0, array.Length);
                    array.AsSpan(0, count).CopyTo(buffer.Span);
                    return count;
                }
                catch (TimeoutException)
                {
                    // SerialPortTransport configures a short ReadTimeout for its own v1 polling
                    // loop; this connector shares the same open port, so a timeout here just means
                    // no v2 bytes have arrived yet — keep waiting rather than treating it as
                    // EOF/failure. (Also works around a known Windows System.IO.Ports gap where
                    // BaseStream.ReadAsync, started before data is pending, can hang forever even
                    // once data arrives — Read() doesn't have this problem.)
                }
            }
        }, cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        if (port.IsOpen) port.Close();
        port.Dispose();
        return ValueTask.CompletedTask;
    }
}
