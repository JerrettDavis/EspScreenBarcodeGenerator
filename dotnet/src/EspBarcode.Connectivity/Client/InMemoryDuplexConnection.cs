using System.Threading.Channels;

namespace EspBarcode.Connectivity.Client;

public sealed class InMemoryDuplexConnection : ILinkConnection
{
    private readonly Channel<byte[]> _outbound;
    private readonly Channel<byte[]> _inbound;
    private byte[] _leftover = [];
    private int _leftoverOffset;

    private InMemoryDuplexConnection(Channel<byte[]> outbound, Channel<byte[]> inbound)
    {
        _outbound = outbound;
        _inbound = inbound;
    }

    public static (ILinkConnection Left, ILinkConnection Right) CreatePair()
    {
        var a = Channel.CreateUnbounded<byte[]>();
        var b = Channel.CreateUnbounded<byte[]>();
        return (new InMemoryDuplexConnection(a, b), new InMemoryDuplexConnection(b, a));
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
        => await _outbound.Writer.WriteAsync(bytes.ToArray(), cancellationToken);

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        if (_leftoverOffset >= _leftover.Length)
        {
            _leftover = await _inbound.Reader.ReadAsync(cancellationToken);
            _leftoverOffset = 0;
        }
        int count = Math.Min(buffer.Length, _leftover.Length - _leftoverOffset);
        _leftover.AsSpan(_leftoverOffset, count).CopyTo(buffer.Span);
        _leftoverOffset += count;
        return count;
    }

    public ValueTask DisposeAsync()
    {
        _outbound.Writer.TryComplete();
        return ValueTask.CompletedTask;
    }
}
