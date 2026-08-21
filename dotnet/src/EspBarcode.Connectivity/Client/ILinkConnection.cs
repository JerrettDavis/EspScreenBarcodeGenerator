namespace EspBarcode.Connectivity.Client;

public interface ILinkConnection : IAsyncDisposable
{
    Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken);
    Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken);
}

public interface ILinkConnector
{
    Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken);
}
