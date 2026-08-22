using System.Runtime.CompilerServices;
using System.Text.Json;
using Microsoft.Playwright;

namespace EspBarcode.Controller.Web.E2ETests.Support;

/// <summary>Per-scenario Playwright state, constructor-injected by Reqnroll into hooks and step classes.</summary>
public sealed class TestWorld : IAsyncDisposable
{
    public IBrowserContext Context { get; private set; } = null!;
    public IPage Page { get; private set; } = null!;

    public const string DefaultFirmware = "0.2.0-fake";

    public async Task InitializeAsync()
    {
        Context = await PlaywrightRunner.Browser!.NewContextAsync();
        await Context.AddInitScriptAsync(scriptPath: FakeSerialScriptPath);
        Page = await Context.NewPageAsync();
        // Cold Blazor WASM boot (multi-MB runtime download + JIT) can occasionally take longer than
        // Playwright's 30s default under a loaded machine; the app logic itself isn't slow.
        Page.SetDefaultTimeout(60000);
        await ConfigureFakeDevicesAsync(authorizedCount: 2);
    }

    /// <summary>(Re)configures the fake Web Serial devices and (re)loads the app so the change applies.</summary>
    public async Task ConfigureFakeDevicesAsync(int authorizedCount, int unauthorizedCount = 0)
    {
        var configs = new List<object>();
        for (var i = 0; i < authorizedCount; i++)
            configs.Add(new { id = $"fake-{i + 1}", authorized = true, firmware = DefaultFirmware, device = "EspScreenBarcodeGenerator" });
        for (var i = 0; i < unauthorizedCount; i++)
            configs.Add(new { id = $"fake-unauth-{i + 1}", authorized = false, firmware = DefaultFirmware });

        var json = JsonSerializer.Serialize(configs);
        await Page.AddInitScriptAsync($"window.__espFakeSerialConfig = {json};");
        await Page.GotoAsync(AppServer.BaseUrl);
        await Page.GetByText("ESP Barcode Control").First.WaitForAsync();
    }

    public async ValueTask DisposeAsync()
    {
        if (Page is not null) await Page.CloseAsync();
        if (Context is not null) await Context.CloseAsync();
    }

    private static readonly string FakeSerialScriptPath = Path.GetFullPath(
        Path.Combine(SourceDirectory(), "..", "..", "..", "src", "EspBarcode.Controller.Web", "wwwroot", "js", "fakeSerial.js"));

    private static string SourceDirectory([CallerFilePath] string path = "") => Path.GetDirectoryName(path)!;
}
