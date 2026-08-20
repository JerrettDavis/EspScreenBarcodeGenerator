using System.Net;
using EspBarcode.Generator;
using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class ViewerClientTests
{
    private sealed class StubHandler(HttpStatusCode status, Action<HttpRequestMessage>? onRequest = null) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            onRequest?.Invoke(request);
            return Task.FromResult(new HttpResponseMessage(status));
        }
    }

    [Fact]
    public void IsHealthy_TwoHundredResponse_ReturnsTrue()
    {
        Assert.True(ViewerClient.IsHealthy(new StubHandler(HttpStatusCode.OK), 47823));
    }

    [Fact]
    public void IsHealthy_ErrorResponse_ReturnsFalse()
    {
        Assert.False(ViewerClient.IsHealthy(new StubHandler(HttpStatusCode.ServiceUnavailable), 47823));
    }

    [Fact]
    public void IsHealthy_HandlerThrows_ReturnsFalse()
    {
        var handler = new ThrowingHandler();
        Assert.False(ViewerClient.IsHealthy(handler, 47823));
    }

    private sealed class ThrowingHandler : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
            => throw new HttpRequestException("connection refused");
    }

    [Fact]
    public void PostRender_TwoHundredResponse_DoesNotThrow()
    {
        HttpRequestMessage? captured = null;
        var handler = new StubHandler(HttpStatusCode.OK, r => captured = r);
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };

        ViewerClient.PostRender(handler, 47823, spec);

        Assert.NotNull(captured);
        Assert.Equal(HttpMethod.Post, captured!.Method);
        Assert.Equal($"/{ViewerProtocol.RenderPath.TrimStart('/')}", captured.RequestUri!.AbsolutePath);
    }

    [Fact]
    public void PostRender_ErrorResponse_ThrowsViewerRenderFailed()
    {
        var handler = new StubHandler(HttpStatusCode.BadRequest);
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };

        var ex = Assert.Throws<BarcodeGenerationException>(() => ViewerClient.PostRender(handler, 47823, spec));
        Assert.Equal("viewer_render_failed", ex.Code);
    }

    [Fact]
    public void PostClose_TwoHundredResponse_DoesNotThrow()
    {
        HttpRequestMessage? captured = null;
        var handler = new StubHandler(HttpStatusCode.OK, r => captured = r);

        ViewerClient.PostClose(handler, 47823);

        Assert.Equal(HttpMethod.Post, captured!.Method);
        Assert.Equal($"/{ViewerProtocol.ClosePath.TrimStart('/')}", captured.RequestUri!.AbsolutePath);
    }
}
