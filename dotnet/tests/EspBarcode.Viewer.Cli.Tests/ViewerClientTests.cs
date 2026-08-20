using System.Net;
using EspBarcode.Generator;
using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class ViewerClientTests
{
    private sealed class StubHandler(HttpStatusCode status, Action<HttpRequestMessage>? onRequest = null, string? body = null) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            onRequest?.Invoke(request);
            var response = new HttpResponseMessage(status);
            if (body is not null) response.Content = new StringContent(body, System.Text.Encoding.UTF8, "application/json");
            return Task.FromResult(response);
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

    // ---- transport failures are the CLI's own error, not a stack trace --------------------------

    private sealed class FailingHandler(Exception failure) : HttpMessageHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
            => throw failure;
    }

    /// <summary>The three shapes a dead loopback endpoint takes: connection refused, the
    /// HttpClient.Timeout the CLI configures elapsing, and a raw socket error.</summary>
    private static Exception TransportFailure(string kind) => kind switch
    {
        "refused" => new HttpRequestException("No connection could be made because the target machine actively refused it."),
        "timeout" => new TaskCanceledException("The request was canceled due to the configured HttpClient.Timeout of 2 seconds elapsing."),
        _ => new System.Net.Sockets.SocketException(10061),
    };

    [Theory]
    [InlineData("refused")]
    [InlineData("timeout")]
    [InlineData("socket")]
    public void PostClose_NothingListening_ThrowsViewerUnreachable(string kind)
    {
        // `close` against a viewer that already exited (or a second `close`) is a documented, normal
        // action; it used to crash with a raw TaskCanceledException after the 2s timeout.
        var ex = Assert.Throws<BarcodeGenerationException>(
            () => ViewerClient.PostClose(new FailingHandler(TransportFailure(kind)), 47999));

        Assert.Equal("viewer_unreachable", ex.Code);
        Assert.Contains("47999", ex.Message);
    }

    [Theory]
    [InlineData("refused")]
    [InlineData("timeout")]
    [InlineData("socket")]
    public void PostRender_TransportFailure_ThrowsViewerUnreachable(string kind)
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };

        var ex = Assert.Throws<BarcodeGenerationException>(
            () => ViewerClient.PostRender(new FailingHandler(TransportFailure(kind)), 47999, spec));

        Assert.Equal("viewer_unreachable", ex.Code);
    }

    // ---- the viewer's structured error reaches the user ----------------------------------------

    [Fact]
    public void PostRender_ErrorResponseWithStructuredBody_CarriesTheViewersCodeAndMessage()
    {
        var handler = new StubHandler(
            HttpStatusCode.BadRequest,
            body: """{"code":"invalid_payload","message":"UPC-A requires digits only, got 'not-digits'."}""");
        var spec = new BarcodeSpec { Type = BarcodeType.UpcA, Data = "not-digits" };

        var ex = Assert.Throws<BarcodeGenerationException>(() => ViewerClient.PostRender(handler, 47823, spec));

        Assert.Equal("viewer_render_failed", ex.Code);
        Assert.Contains("invalid_payload", ex.Message);
        Assert.Contains("UPC-A requires digits only", ex.Message);
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json at all")]
    [InlineData("[1,2,3]")]
    [InlineData("""{"unexpected":"shape"}""")]
    public void PostRender_ErrorResponseWithUnparseableBody_FallsBackToTheStatusCode(string body)
    {
        var handler = new StubHandler(HttpStatusCode.BadRequest, body: body);
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };

        var ex = Assert.Throws<BarcodeGenerationException>(() => ViewerClient.PostRender(handler, 47823, spec));

        Assert.Equal("viewer_render_failed", ex.Code);
        Assert.Contains("400", ex.Message);
    }

    // ---- the /render body speaks the documented wire vocabulary ---------------------------------

    [Fact]
    public void PostRender_RequestBody_SpellsTheTypeAsItsWireValue()
    {
        string? body = null;
        var handler = new StubHandler(HttpStatusCode.OK, r => body = r.Content!.ReadAsStringAsync().GetAwaiter().GetResult());

        ViewerClient.PostRender(handler, 47823, new BarcodeSpec { Type = BarcodeType.DataMatrix, Data = "DM-TEST" });

        Assert.NotNull(body);
        Assert.Contains("\"datamatrix\"", body);
        Assert.DoesNotContain("\"type\":1", body);
    }
}
