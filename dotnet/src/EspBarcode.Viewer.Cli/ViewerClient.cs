using System.Net.Http.Json;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Cli;

internal static class ViewerClient
{
    public static bool IsHealthy(HttpMessageHandler handler, int port)
    {
        try
        {
            using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromMilliseconds(500) };
            var response = client.GetAsync($"http://127.0.0.1:{port}{ViewerProtocol.HealthPath}").GetAwaiter().GetResult();
            return response.IsSuccessStatusCode;
        }
        catch (Exception)
        {
            return false;
        }
    }

    public static void PostRender(HttpMessageHandler handler, int port, BarcodeSpec spec)
    {
        using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(5) };
        var response = client.PostAsJsonAsync($"http://127.0.0.1:{port}{ViewerProtocol.RenderPath}", spec).GetAwaiter().GetResult();
        if (!response.IsSuccessStatusCode)
            throw new BarcodeGenerationException("viewer_render_failed", $"Viewer returned {(int)response.StatusCode} for /render.");
    }

    public static void PostClose(HttpMessageHandler handler, int port)
    {
        using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(2) };
        client.PostAsync($"http://127.0.0.1:{port}{ViewerProtocol.ClosePath}", content: null).GetAwaiter().GetResult();
    }

    public static void Render(BarcodeSpec spec, string[] args)
    {
        var port = ExtractPort(args);
        using var handler = new HttpClientHandler();

        if (!IsHealthy(handler, port))
        {
            LaunchViewer(args, port);
            var attempts = 0;
            while (!IsHealthy(handler, port))
            {
                if (++attempts > 20) throw new BarcodeGenerationException("viewer_launch_failed", "Timed out waiting for EspBarcode.Viewer.Gui to become ready.");
                Thread.Sleep(250);
            }
        }

        PostRender(handler, port, spec);
    }

    public static int Close(string[] args)
    {
        var port = ExtractPort(args);
        using var handler = new HttpClientHandler();
        PostClose(handler, port);
        return 0;
    }

    private static int ExtractPort(string[] args)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == "--viewer-port") return int.Parse(args[i + 1]);
        return ViewerProtocol.DefaultPort;
    }

    private static void LaunchViewer(string[] args, int port)
    {
        string? exePath = null;
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == "--viewer-exe") exePath = args[i + 1];
        exePath ??= Environment.GetEnvironmentVariable("ESP_BARCODE_VIEWER_EXE_PATH");
        exePath ??= Path.Combine(AppContext.BaseDirectory, "EspBarcode.Viewer.Gui.exe");

        if (!File.Exists(exePath))
            throw new BarcodeGenerationException("viewer_exe_not_found", $"Viewer executable not found at '{exePath}'. Pass --viewer-exe or set ESP_BARCODE_VIEWER_EXE_PATH.");

        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(exePath, $"--port {port}") { UseShellExecute = false });
    }
}
