namespace EspBarcode.Controller.Web.Models;

/// <summary>Persisted configuration for "Full Auto Mode" (see <see cref="Services.AutomationService"/>).</summary>
public sealed class AutomationSettings
{
    public bool Enabled { get; set; }
    public bool AutoReconnect { get; set; } = true;
    public int PollIntervalSeconds { get; set; } = 5;
    public bool RotateLibrary { get; set; }
    public int RotateIntervalSeconds { get; set; } = 10;
    public List<string> RotateLibraryItemIds { get; set; } = [];
    public List<string> RotateTargetDeviceIds { get; set; } = [];

    public AutomationSettings Clone() => new()
    {
        Enabled = Enabled,
        AutoReconnect = AutoReconnect,
        PollIntervalSeconds = PollIntervalSeconds,
        RotateLibrary = RotateLibrary,
        RotateIntervalSeconds = RotateIntervalSeconds,
        RotateLibraryItemIds = [.. RotateLibraryItemIds],
        RotateTargetDeviceIds = [.. RotateTargetDeviceIds],
    };
}
