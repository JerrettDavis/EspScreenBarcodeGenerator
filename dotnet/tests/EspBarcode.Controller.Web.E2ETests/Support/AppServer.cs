using System.Diagnostics;
using System.Runtime.CompilerServices;

namespace EspBarcode.Controller.Web.E2ETests.Support;

/// <summary>Starts and stops the Blazor WebAssembly dev server the whole test run drives with Playwright.</summary>
public static class AppServer
{
    private const int Port = 5299;
    public static string BaseUrl { get; } = $"http://127.0.0.1:{Port}";

    private static Process? _process;

    public static async Task StartAsync()
    {
        // A dev server orphaned by a prior run that was killed mid-cleanup (see Hooks.AfterTestRun)
        // would otherwise keep answering on this port with a stale build — silently masking every
        // source change since it started, rather than failing loudly.
        KillAnyExistingListenerOnPort();

        var startInfo = new ProcessStartInfo("dotnet", $"run -c Release --project \"{WebProjectPath}\" --urls {BaseUrl}")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            WorkingDirectory = WebProjectPath,
        };

        _process = Process.Start(startInfo) ?? throw new InvalidOperationException("failed to start the controller app's dev server");
        await WaitUntilReadyAsync();
    }

    public static async Task StopAsync()
    {
        if (_process is null) return;
        try
        {
            if (!_process.HasExited) _process.Kill(entireProcessTree: true);
        }
        catch (InvalidOperationException) { /* already exited */ }
        _process.Dispose();
        _process = null;
        await Task.CompletedTask;
    }

    private static void KillAnyExistingListenerOnPort()
    {
        if (!OperatingSystem.IsWindows()) return;
        try
        {
            using var netstat = Process.Start(new ProcessStartInfo("cmd", $"/c netstat -ano | findstr :{Port}")
            {
                RedirectStandardOutput = true,
                UseShellExecute = false,
            });
            var output = netstat?.StandardOutput.ReadToEnd() ?? "";
            netstat?.WaitForExit(5000);

            foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length < 5 || !parts[1].EndsWith($":{Port}")) continue;
                if (!int.TryParse(parts[^1], out var pid) || pid <= 0) continue;

                try
                {
                    using var stale = Process.GetProcessById(pid);
                    stale.Kill(entireProcessTree: true);
                }
                catch (ArgumentException) { /* already gone */ }
                catch (InvalidOperationException) { /* already exited */ }
            }
        }
        catch
        {
            // Best-effort cleanup only — a failure here just means StartAsync proceeds as before.
        }
    }

    private static async Task WaitUntilReadyAsync()
    {
        using var http = new HttpClient();
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(90);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var response = await http.GetAsync(BaseUrl + "/index.html");
                if (response.IsSuccessStatusCode) return;
            }
            catch (HttpRequestException)
            {
                // Server not listening yet.
            }

            await Task.Delay(500);
        }

        throw new TimeoutException($"Controller app dev server never became ready at {BaseUrl}.");
    }

    // SourceDirectory = dotnet/tests/EspBarcode.Controller.Web.E2ETests/Support
    private static readonly string WebProjectPath =
        Path.GetFullPath(Path.Combine(SourceDirectory(), "..", "..", "..", "src", "EspBarcode.Controller.Web"));

    private static string SourceDirectory([CallerFilePath] string path = "") => Path.GetDirectoryName(path)!;
}
