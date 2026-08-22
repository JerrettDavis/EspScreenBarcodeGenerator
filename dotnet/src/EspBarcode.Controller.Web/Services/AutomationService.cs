using EspBarcode.Controller.Web.Models;

namespace EspBarcode.Controller.Web.Services;

/// <summary>
/// "Full Auto Mode": unattended operation once devices have been paired once. While enabled, it (1)
/// reconnects every browser-authorized serial port on a timer with no user gesture required, (2) polls
/// every connected device's status for the dashboard, and (3) optionally rotates a chosen playlist of
/// library items across a chosen set of devices — a hands-free demo/kiosk loop.
/// </summary>
public sealed class AutomationService : IAsyncDisposable
{
    private const string Key = "esp-controller.automation.v1";

    private readonly DeviceRegistry _registry;
    private readonly BarcodeLibraryService _library;
    private readonly LocalStorageService _storage;
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private int _rotateIndex;

    public AutomationSettings Settings { get; private set; } = new();
    public string? LastActionSummary { get; private set; }
    public event Action? Changed;

    public AutomationService(DeviceRegistry registry, BarcodeLibraryService library, LocalStorageService storage)
    {
        _registry = registry;
        _library = library;
        _storage = storage;
    }

    public async Task InitializeAsync()
    {
        Settings = await _storage.GetAsync<AutomationSettings>(Key) ?? new AutomationSettings();
        if (Settings.Enabled) StartLoop();
    }

    public async Task ApplyAsync(AutomationSettings settings)
    {
        var wasEnabled = Settings.Enabled;
        Settings = settings;
        await _storage.SetAsync(Key, Settings);

        if (Settings.Enabled && !wasEnabled) StartLoop();
        else if (!Settings.Enabled && wasEnabled) await StopLoopAsync();

        Changed?.Invoke();
    }

    private void StartLoop()
    {
        _cts = new CancellationTokenSource();
        _loop = RunLoopAsync(_cts.Token);
    }

    private async Task StopLoopAsync()
    {
        if (_cts is null) return;
        await _cts.CancelAsync();
        if (_loop is not null)
        {
            try { await _loop; } catch (OperationCanceledException) { /* expected on stop */ }
        }
        _cts.Dispose();
        _cts = null;
        _loop = null;
    }

    private async Task RunLoopAsync(CancellationToken cancellationToken)
    {
        var lastRotate = DateTimeOffset.MinValue;
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                if (Settings.AutoReconnect) await _registry.ReconnectAuthorizedAsync(cancellationToken: cancellationToken);

                foreach (var device in _registry.Devices.Where(d => d.IsConnected).ToArray())
                {
                    await device.RefreshAsync(cancellationToken);
                }

                if (Settings.RotateLibrary && Settings.RotateLibraryItemIds.Count > 0 &&
                    DateTimeOffset.UtcNow - lastRotate >= TimeSpan.FromSeconds(Math.Max(1, Settings.RotateIntervalSeconds)))
                {
                    await RotateOnceAsync(cancellationToken);
                    lastRotate = DateTimeOffset.UtcNow;
                }

                Changed?.Invoke();
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                LastActionSummary = $"Auto mode tick failed: {ex.Message}";
                Changed?.Invoke();
            }

            await Task.Delay(TimeSpan.FromSeconds(Math.Max(1, Settings.PollIntervalSeconds)), cancellationToken);
        }
    }

    private async Task RotateOnceAsync(CancellationToken cancellationToken)
    {
        var items = await _library.GetAllAsync();
        var queue = Settings.RotateLibraryItemIds
            .Select(id => items.FirstOrDefault(i => i.Id == id))
            .Where(i => i is not null)
            .Cast<LibraryItem>()
            .ToList();
        if (queue.Count == 0) return;

        var item = queue[_rotateIndex % queue.Count];
        _rotateIndex++;

        var targets = _registry.Devices.Where(d =>
            d.IsConnected && d.Mode == DeviceMode.Client &&
            (Settings.RotateTargetDeviceIds.Count == 0 || Settings.RotateTargetDeviceIds.Contains(d.Id)));

        var failures = 0;
        foreach (var device in targets)
        {
            try { await device.Client.GenerateAsync(item.Options, cancellationToken); }
            catch (Exception) when (cancellationToken is { IsCancellationRequested: false }) { failures++; }
        }

        LastActionSummary = failures == 0
            ? $"Rotated to '{item.Name}' at {DateTimeOffset.Now:T}"
            : $"Rotated to '{item.Name}' at {DateTimeOffset.Now:T} ({failures} device(s) failed)";
    }

    public async ValueTask DisposeAsync() => await StopLoopAsync();
}
