namespace EspBarcode.Controller.Web.Services;

/// <summary>Light/dark, mirroring the on-device theme toggle (lib/UiGeometry/src/Theme.h) 1:1 in CSS
/// (see wwwroot/css/app.css's <c>--esb-*</c> tokens).</summary>
public enum AppTheme { Dark, Light }

public sealed class ThemeService(LocalStorageService storage)
{
    private const string Key = "esp-controller.theme";

    public AppTheme Current { get; private set; } = AppTheme.Dark;
    public event Action? Changed;

    public async Task InitializeAsync()
    {
        var saved = await storage.GetAsync<string>(Key);
        Current = saved == "light" ? AppTheme.Light : AppTheme.Dark;
        Changed?.Invoke();
    }

    public async Task SetAsync(AppTheme theme)
    {
        Current = theme;
        await storage.SetAsync(Key, theme == AppTheme.Light ? "light" : "dark");
        Changed?.Invoke();
    }

    public Task ToggleAsync() => SetAsync(Current == AppTheme.Dark ? AppTheme.Light : AppTheme.Dark);
}
