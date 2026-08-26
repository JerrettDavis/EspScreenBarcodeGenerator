using Microsoft.AspNetCore.Components;
using Microsoft.JSInterop;
namespace EspBarcode.Controller.Web.Services;
public sealed record ImportedBarcode(string Data, string Format);
public sealed class BarcodeImportService(IJSRuntime js) : IAsyncDisposable
{
    private Task<IJSObjectReference>? _module;
    private Task<IJSObjectReference> ModuleAsync() => _module ??= js.InvokeAsync<IJSObjectReference>("import", "./js/barcodeImport.js").AsTask();
    public async Task<bool> IsSupportedAsync() => await (await ModuleAsync()).InvokeAsync<bool>("isSupported");
    public async Task<ImportedBarcode> DecodeAsync(ElementReference input) => await (await ModuleAsync()).InvokeAsync<ImportedBarcode>("decode", input);
    public async ValueTask DisposeAsync() { if (_module is not null) await (await _module).DisposeAsync(); }
}
