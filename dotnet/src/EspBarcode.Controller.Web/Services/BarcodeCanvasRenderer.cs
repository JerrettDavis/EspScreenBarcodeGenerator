using EspBarcode.Controller.Web.Models;
using Microsoft.JSInterop;

namespace EspBarcode.Controller.Web.Services;

/// <summary>
/// Renders a downloaded module matrix onto an HTML canvas. There is no host-side rasterizer here (the
/// existing <c>EspBarcode.Generator</c> library depends on <c>System.Drawing.Common</c>, which is
/// Windows-only and unavailable inside the browser WASM sandbox) — instead this renders whatever the
/// device itself produced, downloaded via the v1 <c>download</c> command, which is guaranteed
/// pixel-accurate to the physical screen.
/// </summary>
public sealed class BarcodeCanvasRenderer(IJSRuntime js)
{
    private IJSObjectReference? _module;

    private async Task<IJSObjectReference> ModuleAsync()
        => _module ??= await js.InvokeAsync<IJSObjectReference>("import", "./js/barcodeCanvas.js");

    public async Task RenderAsync(string canvasElementId, DownloadedMatrix matrix)
    {
        var module = await ModuleAsync();
        await module.InvokeVoidAsync("renderMatrix", canvasElementId, matrix.Width, matrix.Height, matrix.Invert,
            Convert.ToBase64String(matrix.Packed));
    }
}
