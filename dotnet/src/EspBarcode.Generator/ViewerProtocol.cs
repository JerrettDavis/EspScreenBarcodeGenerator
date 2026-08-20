namespace EspBarcode.Generator;

/// <summary>Shared contract between EspBarcode.Viewer.Cli and EspBarcode.Viewer.Gui's loopback HTTP server.</summary>
public static class ViewerProtocol
{
    public const int DefaultPort = 47823;
    public const string HealthPath = "/health";
    public const string RenderPath = "/render";
    public const string ClosePath = "/close";
}
