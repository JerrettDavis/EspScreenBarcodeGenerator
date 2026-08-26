using System.Threading.Channels;
using EspBarcode.Connectivity.Client;
using Microsoft.JSInterop;
namespace EspBarcode.Controller.Web.Services;
public sealed class WebBluetoothConnection : ILinkConnection
{
    private readonly WebBluetoothModule _module;
    private readonly Channel<byte[]> _incoming = Channel.CreateUnbounded<byte[]>();
    private readonly DotNetObjectReference<WebBluetoothConnection> _reference;
    private string? _id;
    private WebBluetoothConnection(WebBluetoothModule module) { _module = module; _reference = DotNetObjectReference.Create(this); }
    public string Id => _id ?? throw new InvalidOperationException("Bluetooth connection is not open.");
    public string Name { get; private set; } = "ESP Barcode Display";
    public int MaxFrameBytes { get; private set; } = 200;
    public static async Task<WebBluetoothConnection> OpenAsync(WebBluetoothModule module, CancellationToken ct)
    {
        var result = new WebBluetoothConnection(module); var handle = await module.ConnectAsync(result._reference, ct);
        result._id = handle.Id; result.Name = handle.Name; result.MaxFrameBytes = handle.MaxFrameBytes; return result;
    }
    [JSInvokable] public void OnBluetoothData(string base64) => _incoming.Writer.TryWrite(Convert.FromBase64String(base64));
    [JSInvokable] public void OnBluetoothClosed() => _incoming.Writer.TryComplete(new IOException("Bluetooth device disconnected."));
    public Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken ct) => _module.WriteAsync(Id, bytes.ToArray(), ct);
    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken ct) { var bytes = await _incoming.Reader.ReadAsync(ct); if (bytes.Length > buffer.Length) throw new InvalidOperationException("Bluetooth frame exceeds receive buffer."); bytes.CopyTo(buffer); return bytes.Length; }
    public async ValueTask DisposeAsync() { if (_id is not null) await _module.DisconnectAsync(_id); _incoming.Writer.TryComplete(); _reference.Dispose(); }
}
