using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using EspBarcode.Generator;

namespace EspBarcode.Viewer.Gui;

public partial class MainWindow : Window
{
    private BarcodeSpec? _currentSpec;

    public MainWindow()
    {
        InitializeComponent();

        // Re-lay-out against the *drawing surface* whenever it changes size. RootGrid (rather than
        // the Window) is the canvas the rendered PNG has to match: Window.ActualWidth/Height include
        // the border and title bar, so fitting to those would push part of the symbol under the
        // window chrome.
        RootGrid.SizeChanged += (_, _) => TryRelayoutCurrentSpec();
    }

    /// <summary>Adopts <paramref name="spec"/> as the current spec and renders it. Callable from any
    /// thread; marshals onto the WPF UI thread before touching any element.</summary>
    /// <exception cref="Exception">Whatever generation threw. The previously displayed spec is kept
    /// and the viewer stays up; the caller decides how to report it.</exception>
    public void ShowSpec(BarcodeSpec spec)
    {
        Exception? failure = null;

        Dispatcher.Invoke(() =>
        {
            var previous = _currentSpec;
            _currentSpec = spec;
            try
            {
                RelayoutCurrentSpec();
            }
            catch (Exception ex)
            {
                // Captured rather than thrown out of the dispatcher callback: an exception escaping
                // a DispatcherOperation is routed through Application.DispatcherUnhandledException
                // first, which would tear this long-lived viewer down over one bad request. Note
                // that BarcodeGenerator.Encode does not wrap every ZXing failure in a
                // BarcodeGenerationException (e.g. Code 39 with a non-encodable character surfaces
                // as a raw ArgumentException), so this deliberately catches Exception.
                _currentSpec = previous;
                failure = ex;
                return;
            }

            BringToFront();
        });

        if (failure is not null) throw failure;
    }

    private void TryRelayoutCurrentSpec()
    {
        try
        {
            RelayoutCurrentSpec();
        }
        catch (Exception ex)
        {
            // Shrinking the window past the symbol's minimum module size is a normal user action,
            // not a crash: surface it in place and pick the symbol back up when they grow it again.
            // Nothing here runs on a request thread, so an unhandled exception would take the
            // process down instead of being reportable — show it rather than die.
            BarcodeImage.Source = null;
            RootGrid.Background = Brushes.White; // an inverted spec leaves a black ground the message can't be read on
            StatusText.Text = ex.Message;
            StatusText.Visibility = Visibility.Visible;
        }
    }

    private void RelayoutCurrentSpec()
    {
        if (_currentSpec is null) return;

        var spec = _currentSpec;

        // A /render that lands before the first layout pass would otherwise fit against a 0x0 canvas.
        if (RootGrid.ActualWidth < 1 || RootGrid.ActualHeight < 1) UpdateLayout();

        // Fit against *device* pixels, not device-independent units: a barcode is only scannable
        // while every module keeps its exact integer pixel size, and on a 125%/150% display a
        // DIP-sized bitmap would be resampled on its way to the screen.
        var dpi = VisualTreeHelper.GetDpi(this);
        var canvasWidth = Math.Max(1, (int)(RootGrid.ActualWidth * dpi.DpiScaleX));
        var canvasHeight = Math.Max(1, (int)(RootGrid.ActualHeight * dpi.DpiScaleY));

        var matrix = BarcodeGenerator.Encode(spec);
        var quiet = ScreenFitLayout.ResolveQuietZone(spec.Type, spec.Quiet);
        var layout = ScreenFitLayout.Fit(matrix, quiet, spec.MinModule, spec.Rotation, canvasWidth, canvasHeight);
        var png = BarcodeImageRenderer.Render(layout, spec.Invert);

        var bitmap = new BitmapImage();
        using (var stream = new MemoryStream(png))
        {
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.StreamSource = stream;
            bitmap.EndInit();
        }
        bitmap.Freeze();

        RootGrid.Background = spec.Invert ? Brushes.Black : Brushes.White;
        BarcodeImage.Source = bitmap;
        BarcodeImage.Width = canvasWidth / dpi.DpiScaleX;
        BarcodeImage.Height = canvasHeight / dpi.DpiScaleY;
        StatusText.Visibility = Visibility.Collapsed;
    }

    private void BringToFront()
    {
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;

        // Activate() alone is routinely ignored when another process owns the foreground (the
        // scanning workflow's whole point is that the CLI in some other terminal raises this
        // window), so nudge it through the topmost band for one cycle.
        Topmost = true;
        Topmost = false;
        Activate();
    }
}
