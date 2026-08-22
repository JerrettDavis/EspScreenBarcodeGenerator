using System.Text;
using System.Threading.Channels;
using EspBarcode.Connectivity.Client;
using Microsoft.JSInterop;

namespace EspBarcode.Controller.Web.Services;

/// <summary>
/// One open Web Serial port, exposed two ways over the same byte stream: <see cref="ReadLineAsync"/>
/// for the NDJSON v1 protocol, and <see cref="ILinkConnection"/> (raw bytes) for the EspLink v2 stack
/// once a device has been asked to <c>upgrade</c>/<c>gateway</c>. This mirrors
/// <c>EspBarcode.Client.TransportV2.SerialLinkConnection</c>, which reuses the same open
/// <see cref="System.IO.Ports.SerialPort"/> across the same v1-then-v2 transition — here the "port" is
/// a Web Serial <c>SerialPort</c> JS object instead, reached through <see cref="WebSerialModule"/>.
/// </summary>
public sealed class WebSerialConnection : ILinkConnection
{
    private readonly WebSerialModule _module;
    private readonly Channel<byte> _incoming = Channel.CreateUnbounded<byte>();
    private readonly DotNetObjectReference<WebSerialConnection> _selfRef;
    private Task? _readPump;
    private int _disposed;

    public string PortId { get; }
    public event Action? Closed;

    private WebSerialConnection(WebSerialModule module, string portId)
    {
        _module = module;
        PortId = portId;
        _selfRef = DotNetObjectReference.Create(this);
    }

    public static async Task<WebSerialConnection> OpenAsync(
        WebSerialModule module, string portId, int baudRate, CancellationToken cancellationToken)
    {
        await module.OpenAsync(portId, baudRate, cancellationToken);
        var connection = new WebSerialConnection(module, portId);
        connection._readPump = connection.PumpAsync(cancellationToken);
        return connection;
    }

    private async Task PumpAsync(CancellationToken cancellationToken)
    {
        try
        {
            // Runs until the JS side reports the stream closed (see OnSerialClosed below).
            await _module.StartReadingAsync(PortId, _selfRef, cancellationToken);
        }
        catch (JSDisconnectedException)
        {
            // Circuit/runtime tearing down; nothing left to report to.
        }
    }

    [JSInvokable]
    public void OnSerialData(string base64)
    {
        var bytes = Convert.FromBase64String(base64);
        foreach (var b in bytes) _incoming.Writer.TryWrite(b);
    }

    [JSInvokable]
    public void OnSerialClosed()
    {
        _incoming.Writer.TryComplete();
        Closed?.Invoke();
    }

    /// <summary>Reads one NDJSON line (v1 protocol), or null if the port closed with a partial/no line.</summary>
    public async Task<string?> ReadLineAsync(CancellationToken cancellationToken)
    {
        var buffer = new List<byte>();
        try
        {
            while (true)
            {
                var b = await _incoming.Reader.ReadAsync(cancellationToken);
                if (b == (byte)'\n') return Encoding.UTF8.GetString([.. buffer]);
                if (b != (byte)'\r') buffer.Add(b);
            }
        }
        catch (ChannelClosedException)
        {
            return buffer.Count > 0 ? Encoding.UTF8.GetString([.. buffer]) : null;
        }
    }

    public async Task WriteLineAsync(string line, CancellationToken cancellationToken)
        => await WriteAsync(Encoding.UTF8.GetBytes(line + "\n"), cancellationToken);

    // ---- ILinkConnection (EspLink v2, raw bytes) ----

    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
        => await _module.WriteAsync(PortId, Convert.ToBase64String(bytes.Span), cancellationToken);

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        byte first;
        try
        {
            first = await _incoming.Reader.ReadAsync(cancellationToken);
        }
        catch (ChannelClosedException)
        {
            return 0;
        }

        var span = buffer.Span;
        span[0] = first;
        var count = 1;
        while (count < span.Length && _incoming.Reader.TryRead(out var next)) span[count++] = next;
        return count;
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        await _module.CloseAsync(PortId);
        if (_readPump is not null)
        {
            try { await _readPump; } catch { /* pump surfaces its own errors via Closed */ }
        }
        _selfRef.Dispose();
    }
}
