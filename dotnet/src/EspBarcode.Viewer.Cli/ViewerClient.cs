using System.Globalization;
using System.Net.Http.Json;
using System.Net.Sockets;
using System.Text.Json;
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
            // "Nothing is listening" is the expected answer here, not an error: this probe is how the
            // CLI decides whether it has to launch the viewer at all.
            return false;
        }
    }

    public static void PostRender(HttpMessageHandler handler, int port, BarcodeSpec spec)
    {
        using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(5) };
        var response = Transport(port, ViewerProtocol.RenderPath,
            () => client.PostAsJsonAsync($"http://127.0.0.1:{port}{ViewerProtocol.RenderPath}", spec).GetAwaiter().GetResult());

        using (response)
        {
            if (response.IsSuccessStatusCode) return;

            // The viewer answers a rejected spec with {"code":..,"message":..} naming the actual
            // reason ("UPC-A check digit mismatch ..."). Reporting only the status code would throw
            // that away and leave the user guessing which option was wrong.
            var detail = DescribeErrorBody(response);
            throw new BarcodeGenerationException(
                "viewer_render_failed",
                detail is null
                    ? $"Viewer returned {(int)response.StatusCode} for {ViewerProtocol.RenderPath}."
                    : $"Viewer rejected the request (HTTP {(int)response.StatusCode}): {detail}");
        }
    }

    public static void PostClose(HttpMessageHandler handler, int port)
    {
        using var client = new HttpClient(handler, disposeHandler: false) { Timeout = TimeSpan.FromSeconds(2) };
        // The viewer shuts itself down right after answering, so there is nothing in the response
        // worth inspecting — only the transport failure matters.
        Transport(port, ViewerProtocol.ClosePath,
            () => client.PostAsync($"http://127.0.0.1:{port}{ViewerProtocol.ClosePath}", content: null).GetAwaiter().GetResult())
            .Dispose();
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
        RejectUnrecognizedCloseOptions(args);
        var port = ExtractPort(args);
        using var handler = new HttpClientHandler();
        PostClose(handler, port);
        return 0;
    }

    /// <summary>
    /// Runs one HTTP exchange, turning every transport-level failure into the CLI's own error
    /// contract. Closing a viewer that already exited (or running <c>close</c> twice) is a normal
    /// action, and it used to surface as a raw <see cref="TaskCanceledException"/> stack trace after
    /// the client's timeout elapsed.
    /// </summary>
    private static HttpResponseMessage Transport(int port, string path, Func<HttpResponseMessage> send)
    {
        try
        {
            return send();
        }
        catch (Exception ex) when (ex is HttpRequestException or OperationCanceledException or SocketException)
        {
            // OperationCanceledException covers TaskCanceledException, which is what HttpClient
            // raises on its own timeout; HttpRequestException covers connection refused / DNS.
            throw new BarcodeGenerationException(
                "viewer_unreachable",
                $"Could not reach the viewer at http://127.0.0.1:{port}{path} ({ex.Message}) — is EspBarcode.Viewer.Gui running on port {port}?");
        }
    }

    /// <summary>Pulls <c>code</c>/<c>message</c> out of the viewer's JSON error body, or null when
    /// the body is empty, not JSON, or shaped differently — a malformed body must not turn a clean
    /// error report into a parse crash.</summary>
    private static string? DescribeErrorBody(HttpResponseMessage response)
    {
        try
        {
            var body = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
            if (string.IsNullOrWhiteSpace(body)) return null;

            using var document = JsonDocument.Parse(body);
            if (document.RootElement.ValueKind != JsonValueKind.Object) return null;

            var message = ReadStringProperty(document.RootElement, "message");
            if (message is null) return null;

            var code = ReadStringProperty(document.RootElement, "code");
            return code is null ? message : $"[{code}] {message}";
        }
        catch (Exception)
        {
            return null;
        }
    }

    /// <summary>Case-insensitive lookup: the viewer serializes with web defaults (camelCase), but the
    /// CLI should not break if that ever changes.</summary>
    private static string? ReadStringProperty(JsonElement element, string name)
    {
        foreach (var property in element.EnumerateObject())
        {
            if (!string.Equals(property.Name, name, StringComparison.OrdinalIgnoreCase)) continue;
            return property.Value.ValueKind == JsonValueKind.String ? property.Value.GetString() : null;
        }
        return null;
    }

    private static void RejectUnrecognizedCloseOptions(string[] args)
    {
        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] != "--viewer-port")
            {
                if (args[i].StartsWith("--", StringComparison.Ordinal))
                    throw new BarcodeGenerationException("invalid_args", $"Unknown option '{args[i]}'.");
                continue;
            }

            if (i + 1 >= args.Length)
                throw new BarcodeGenerationException("invalid_args", "Option '--viewer-port' requires a value.");
            i++;
        }
    }

    private static int ExtractPort(string[] args)
    {
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (args[i] != "--viewer-port") continue;
            if (!int.TryParse(args[i + 1], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var port))
                throw new BarcodeGenerationException("invalid_args", $"Option '--viewer-port' requires an integer, got '{args[i + 1]}'.");
            return port;
        }
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

        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(exePath, $"--port {port}") { UseShellExecute = false });
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException or System.IO.IOException)
        {
            throw new BarcodeGenerationException("viewer_launch_failed", $"Could not start '{exePath}': {ex.Message}");
        }
    }
}
