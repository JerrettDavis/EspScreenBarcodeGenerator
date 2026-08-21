using System.Buffers.Binary;
using System.Threading.Channels;
using EspBarcode.Connectivity.Client;
using Windows.Networking.Sockets;
using Windows.Storage.Streams;

namespace EspBarcode.Client.WifiDirect;

/// <summary>
/// One connected EspLink v2 TCP session over the "Wi-Fi Direct TCP" carrier framing
/// (docs/PROTOCOL_V2.md §8.6): <c>uint32_le frameLength + raw hop frame</c>, mirroring the
/// firmware's <c>LengthPrefixFrameParser</c> (<c>lib/EspLinkCore/src/LengthPrefixFraming.h</c>).
/// <see cref="ReadAsync"/> hands out exactly one hop frame's bytes per call, matching the
/// contract <see cref="EspBarcode.Connectivity.Client.EspLinkDatagramLinkSession"/> relies on.
/// </summary>
public sealed class WifiDirectTcpLinkConnection : ILinkConnection
{
    // Protocol ceiling for tcp-large (docs/PROTOCOL_V2.md §8.5) — a declared length above this
    // is treated as a corrupt/misbehaving peer rather than an oversized-but-legitimate frame.
    private const uint MaxFrameBytes = 65_535;

    private readonly StreamSocket _socket;
    private readonly DataWriter _writer;
    private readonly DataReader _reader;
    private readonly Channel<byte[]> _incoming = Channel.CreateUnbounded<byte[]>();
    private readonly CancellationTokenSource _pumpCts = new();
    private readonly Task _pumpTask;

    public WifiDirectTcpLinkConnection(StreamSocket socket)
    {
        _socket = socket;
        _writer = new DataWriter(socket.OutputStream);
        _reader = new DataReader(socket.InputStream);
        _pumpTask = Task.Run(() => PumpReceiveAsync(_pumpCts.Token));
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
    {
        var framed = new byte[4 + bytes.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(framed, (uint)bytes.Length);
        bytes.Span.CopyTo(framed.AsSpan(4));
        _writer.WriteBytes(framed);
        await _writer.StoreAsync().AsTask(cancellationToken);
    }

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        byte[] message;
        try
        {
            message = await _incoming.Reader.ReadAsync(cancellationToken);
        }
        catch (ChannelClosedException)
        {
            return 0;
        }

        if (message.Length > buffer.Length)
            throw new InvalidOperationException(
                $"received a {message.Length}-byte Wi-Fi Direct TCP frame but the read buffer is only {buffer.Length} bytes");
        message.CopyTo(buffer);
        return message.Length;
    }

    private async Task PumpReceiveAsync(CancellationToken cancellationToken)
    {
        try
        {
            var lengthBuffer = new byte[4];
            while (!cancellationToken.IsCancellationRequested)
            {
                await ReadExactAsync(lengthBuffer, cancellationToken);
                uint length = BinaryPrimitives.ReadUInt32LittleEndian(lengthBuffer);
                if (length == 0 || length > MaxFrameBytes)
                    throw new InvalidOperationException($"peer declared an invalid frame length ({length} bytes)");

                var frame = new byte[length];
                await ReadExactAsync(frame, cancellationToken);
                await _incoming.Writer.WriteAsync(frame, cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
            // normal shutdown via DisposeAsync
        }
        catch (Exception)
        {
            // connection dropped or protocol violation — ReceiveMessagesAsync sees this as end-of-stream
        }
        finally
        {
            _incoming.Writer.TryComplete();
        }
    }

    private async Task ReadExactAsync(byte[] buffer, CancellationToken cancellationToken)
    {
        int offset = 0;
        while (offset < buffer.Length)
        {
            uint loaded = await _reader.LoadAsync((uint)(buffer.Length - offset)).AsTask(cancellationToken);
            if (loaded == 0) throw new IOException("Wi-Fi Direct TCP connection closed by remote");

            var chunk = new byte[loaded];
            _reader.ReadBytes(chunk);
            chunk.CopyTo(buffer, offset);
            offset += (int)loaded;
        }
    }

    public async ValueTask DisposeAsync()
    {
        await _pumpCts.CancelAsync();
        try { await _pumpTask; } catch { /* observed above */ }

        _incoming.Writer.TryComplete();
        _writer.Dispose();
        _reader.Dispose();
        _socket.Dispose();
    }
}
