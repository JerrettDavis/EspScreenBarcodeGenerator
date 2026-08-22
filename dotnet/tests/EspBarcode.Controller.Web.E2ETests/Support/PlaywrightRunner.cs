using Microsoft.Playwright;

namespace EspBarcode.Controller.Web.E2ETests.Support;

/// <summary>One Chromium instance shared across the whole test run.</summary>
public static class PlaywrightRunner
{
    private static IPlaywright? _playwright;
    public static IBrowser? Browser { get; private set; }

    public static async Task StartAsync()
    {
        _playwright = await Playwright.CreateAsync();
        Browser = await _playwright.Chromium.LaunchAsync(new BrowserTypeLaunchOptions { Headless = true });
    }

    public static async Task StopAsync()
    {
        if (Browser is not null) await Browser.CloseAsync();
        _playwright?.Dispose();
    }
}
