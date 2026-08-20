using System.IO;
using System.Net.Sockets;
using System.Windows;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Gui;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var port = ParsePort(e.Args);

        // The window is created here rather than via App.xaml's StartupUri: WPF only honours
        // StartupUri *after* OnStartup returns, so Application.MainWindow is still null at this
        // point. Creating it explicitly also lets us bail out without ever flashing a window when
        // the port is already owned by another viewer instance.
        var window = new MainWindow();
        MainWindow = window;

        try
        {
            ViewerHost.Start(port, window);
        }
        catch (Exception ex) when (ex is IOException or SocketException)
        {
            MessageBox.Show(
                $"A viewer is already running on port {port}.",
                "EspBarcode Viewer",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
            Shutdown();
            return;
        }

        window.Show();
    }

    private static int ParsePort(string[] args)
    {
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] == "--port") return int.Parse(args[i + 1]);
        var env = Environment.GetEnvironmentVariable("ESP_BARCODE_VIEWER_PORT");
        return env is not null ? int.Parse(env) : ViewerProtocol.DefaultPort;
    }
}
