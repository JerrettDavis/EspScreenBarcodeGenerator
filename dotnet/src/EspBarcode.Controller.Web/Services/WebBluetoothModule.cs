using Microsoft.JSInterop;
namespace EspBarcode.Controller.Web.Services;
public sealed record BluetoothHandle(string Id, string Name, int MaxFrameBytes);
public sealed class WebBluetoothModule(IJSRuntime js) : IAsyncDisposable
{
    private Task<IJSObjectReference>? _module;
    private Task<IJSObjectReference> ModuleAsync() => _module ??= js.InvokeAsync<IJSObjectReference>("import", "./js/webBluetooth.js").AsTask();
    public async Task<bool> IsSupportedAsync() => await (await ModuleAsync()).InvokeAsync<bool>("isSupported");
    public async Task<BluetoothHandle> ConnectAsync(DotNetObjectReference<WebBluetoothConnection> reference, CancellationToken ct) => await (await ModuleAsync()).InvokeAsync<BluetoothHandle>("connect", ct, reference);
    public async Task WriteAsync(string id, byte[] bytes, CancellationToken ct) => await (await ModuleAsync()).InvokeVoidAsync("write", ct, id, Convert.ToBase64String(bytes));
    public async Task DisconnectAsync(string id) => await (await ModuleAsync()).InvokeVoidAsync("disconnect", id);
    public async ValueTask DisposeAsync() { if (_module is not null) await (await _module).DisposeAsync(); }
}
