using Microsoft.JSInterop;

namespace EspBarcode.Controller.Web.Services;

public sealed record SerialPortHandle(string Id, int? UsbVendorId, int? UsbProductId);

/// <summary>
/// Thin JS-interop facade over <c>wwwroot/js/webSerial.js</c>, i.e. the Web Serial API. Every method
/// here does exactly what its JS counterpart does — no protocol knowledge lives at this layer, that's
/// <see cref="WebSerialConnection"/> and up.
/// </summary>
public sealed class WebSerialModule(IJSRuntime js) : IAsyncDisposable
{
    private readonly Lock _gate = new();
    private Task<IJSObjectReference>? _moduleTask;

    private Task<IJSObjectReference> ModuleAsync()
    {
        lock (_gate)
        {
            return _moduleTask ??= js.InvokeAsync<IJSObjectReference>("import", "./js/webSerial.js").AsTask();
        }
    }

    public async Task<bool> IsSupportedAsync()
        => await (await ModuleAsync()).InvokeAsync<bool>("isSupported");

    public async Task<SerialPortHandle> RequestPortAsync(CancellationToken ct)
        => await (await ModuleAsync()).InvokeAsync<SerialPortHandle>("requestPort", ct);

    public async Task<List<SerialPortHandle>> GetAuthorizedPortsAsync(CancellationToken ct)
        => await (await ModuleAsync()).InvokeAsync<List<SerialPortHandle>>("getAuthorizedPorts", ct);

    public async Task OpenAsync(string portId, int baudRate, CancellationToken ct)
        => await (await ModuleAsync()).InvokeVoidAsync("open", ct, portId, baudRate);

    /// <summary>Kicks off the JS read pump. Does not return until the port closes — call without awaiting.</summary>
    public async Task StartReadingAsync(string portId, DotNetObjectReference<WebSerialConnection> selfRef, CancellationToken ct)
        => await (await ModuleAsync()).InvokeVoidAsync("startReading", ct, portId, selfRef);

    public async Task WriteAsync(string portId, string base64Bytes, CancellationToken ct)
        => await (await ModuleAsync()).InvokeVoidAsync("write", ct, portId, base64Bytes);

    public async Task CloseAsync(string portId)
        => await (await ModuleAsync()).InvokeVoidAsync("close", CancellationToken.None, portId);

    public async Task ForgetAsync(string portId)
        => await (await ModuleAsync()).InvokeVoidAsync("forget", CancellationToken.None, portId);

    public async ValueTask DisposeAsync()
    {
        if (_moduleTask is null) return;
        var module = await _moduleTask;
        await module.DisposeAsync();
    }
}
