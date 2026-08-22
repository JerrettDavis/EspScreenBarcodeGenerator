using System.Text.Json;
using Microsoft.JSInterop;

namespace EspBarcode.Controller.Web.Services;

/// <summary>JSON-over-localStorage persistence — everything this controller keeps across page reloads
/// (library items, device nicknames, automation settings, theme) lives here rather than on a server,
/// since the app is a standalone Blazor WebAssembly page with no backend.</summary>
public sealed class LocalStorageService(IJSRuntime js)
{
    private static readonly JsonSerializerOptions Options = new(JsonSerializerDefaults.Web);

    public async Task<T?> GetAsync<T>(string key)
    {
        var json = await js.InvokeAsync<string?>("localStorage.getItem", key);
        if (string.IsNullOrEmpty(json)) return default;
        try { return JsonSerializer.Deserialize<T>(json, Options); }
        catch (JsonException) { return default; }
    }

    public async Task SetAsync<T>(string key, T value)
        => await js.InvokeVoidAsync("localStorage.setItem", key, JsonSerializer.Serialize(value, Options));

    public async Task RemoveAsync(string key) => await js.InvokeVoidAsync("localStorage.removeItem", key);
}
