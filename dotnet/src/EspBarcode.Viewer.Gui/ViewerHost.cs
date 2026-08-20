using System.Net;
using System.Windows;
using EspBarcode.Generator;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;

namespace EspBarcode.Viewer.Gui;

internal static class ViewerHost
{
    /// <summary>Starts the loopback control server. Returns only once Kestrel is actually listening,
    /// so a port collision surfaces to the caller as an <see cref="System.IO.IOException"/> instead of being
    /// swallowed by a fire-and-forget task.</summary>
    public static void Start(int port, MainWindow window)
    {
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions
        {
            // The viewer is usually launched by the CLI, so the process working directory is
            // whatever the caller happened to be in. Pin the content root to the app directory so
            // host configuration never depends on it.
            ContentRootPath = AppContext.BaseDirectory,
        });
        builder.WebHost.ConfigureKestrel(options => options.Listen(IPAddress.Loopback, port));
        builder.Logging.ClearProviders();
        var app = builder.Build();

        app.MapGet(ViewerProtocol.HealthPath, () => Results.Ok());

        app.MapPost(ViewerProtocol.RenderPath, (BarcodeSpec spec) =>
        {
            try
            {
                window.ShowSpec(spec);
                return Results.Ok();
            }
            catch (BarcodeGenerationException ex)
            {
                return Results.BadRequest(new { ex.Code, ex.Message });
            }
            catch (Exception ex)
            {
                // Defense in depth, not the primary guard: BarcodeGenerator.Encode now translates
                // every ZXing failure it can raise into a BarcodeGenerationException at the source,
                // so the catch above is what a bad payload actually lands in. This one keeps an
                // unforeseen failure anywhere else in the render path (image encoding, WPF interop)
                // from taking the viewer process down mid-session.
                return Results.Json(new { Code = "render_failed", ex.Message }, statusCode: 500);
            }
        });

        app.MapPost(ViewerProtocol.ClosePath, (HttpContext context) =>
        {
            // Shut down only once the 200 is on the wire; tearing the process down from inside the
            // handler would reset the connection and fail the caller's request.
            context.Response.OnCompleted(() =>
            {
                window.Dispatcher.BeginInvoke(() => Application.Current.Shutdown());
                return Task.CompletedTask;
            });
            return Results.Ok();
        });

        // Started on the thread pool and waited on: StartAsync's continuations must not need the WPF
        // UI thread, which is blocked right here inside Application.OnStartup.
        Task.Run(() => app.StartAsync()).GetAwaiter().GetResult();
    }
}
