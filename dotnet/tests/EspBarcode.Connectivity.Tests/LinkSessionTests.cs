using EspBarcode.Connectivity.Client;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Tests;

public class LinkSessionTests
{
    [Fact]
    public async Task ControlSession_UsesConfiguredCarrierProfile()
    {
        await using var link = new RecordingMessageLink();
        await using var client = new EspLinkControlSession(link, CarrierProfileId.BleGattV1);
        client.Start();
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

        var send = client.SendCommandAsync(ServiceId.System, "{}"u8.ToArray(), 0, TimeSpan.FromSeconds(5), cts.Token, routeId: 42);
        await link.MessageSent.Task.WaitAsync(cts.Token);

        Assert.Equal(CarrierProfileId.BleGattV1, link.Profile);
        Assert.Equal((ushort)42, link.RouteId);
        Assert.True(MessageEnvelope.TryDecode(link.Message!, out var request, out _, out _));
        var response = new MessageEnvelope { Kind = MessageKind.Result, OperationId = 2, CorrelationId = request.OperationId };
        Assert.True(MessageEnvelope.TryEncode(response, "{}"u8, out var encoded, out _));
        await link.Responses.Writer.WriteAsync(encoded, cts.Token);
        await send;
    }

    [Fact]
    public async Task SendMessage_IsReceivedIntactOnTheOtherEndOfALoopback()
    {
        var (left, right) = InMemoryDuplexConnection.CreatePair();
        await using var leftSession = new EspLinkLinkSession(left, linkSessionId: 1);
        await using var rightSession = new EspLinkLinkSession(right, linkSessionId: 1);

        var body = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.ping\",\"body\":{}}"u8.ToArray();
        var envelope = new MessageEnvelope { OperationId = 1 };
        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out _));

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var receiveTask = rightSession.ReceiveMessagesAsync(cts.Token).GetAsyncEnumerator();

        await leftSession.SendMessageAsync(message, TrafficClass.Control, CarrierProfileId.StreamStandard, cts.Token);

        Assert.True(await receiveTask.MoveNextAsync());
        Assert.Equal(message, receiveTask.Current);
    }

    [Fact]
    public async Task ControlSession_CorrelatesRequestAndResponseAcrossALoopback()
    {
        var (clientConn, deviceConn) = InMemoryDuplexConnection.CreatePair();
        await using var clientLink = new EspLinkLinkSession(clientConn, linkSessionId: 1);
        await using var deviceLink = new EspLinkLinkSession(deviceConn, linkSessionId: 1);
        await using var client = new EspLinkControlSession(clientLink);
        client.Start();

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

        // Minimal device stub: echo back a Result envelope correlated to whatever it receives.
        var deviceTask = Task.Run(async () =>
        {
            await foreach (var incoming in deviceLink.ReceiveMessagesAsync(cts.Token))
            {
                Assert.True(MessageEnvelope.TryDecode(incoming, out var requestEnvelope, out _, out _));
                var responseBody = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.hello\",\"body\":{}}"u8.ToArray();
                var responseEnvelope = new MessageEnvelope
                {
                    Kind = MessageKind.Result,
                    CorrelationId = requestEnvelope.OperationId,
                    OperationId = 1000,
                };
                Assert.True(MessageEnvelope.TryEncode(responseEnvelope, responseBody, out var responseMessage, out _));
                await deviceLink.SendMessageAsync(responseMessage, TrafficClass.Control, CarrierProfileId.StreamStandard, cts.Token);
                break;
            }
        }, cts.Token);

        var requestBody = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.hello\",\"body\":{}}"u8.ToArray();
        var (responseEnvelope, responseBody) = await client.SendCommandAsync(
            ServiceId.System, requestBody, controlSessionId: 0, TimeSpan.FromSeconds(5), cts.Token);

        Assert.Equal(MessageKind.Result, responseEnvelope.Kind);
        await deviceTask;
    }
}

file sealed class RecordingMessageLink : IMessageLinkSession
{
    public System.Threading.Channels.Channel<byte[]> Responses { get; } = System.Threading.Channels.Channel.CreateUnbounded<byte[]>();
    public TaskCompletionSource MessageSent { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public CarrierProfileId? Profile { get; private set; }
    public ushort RouteId { get; private set; }
    public byte[]? Message { get; private set; }
    public Task SendMessageAsync(byte[] layer3Message, TrafficClass trafficClass, CarrierProfileId profileId, CancellationToken cancellationToken, ushort routeId = 0)
    { Message = layer3Message; Profile = profileId; RouteId = routeId; MessageSent.TrySetResult(); return Task.CompletedTask; }
    public async IAsyncEnumerable<byte[]> ReceiveMessagesAsync([System.Runtime.CompilerServices.EnumeratorCancellation] CancellationToken cancellationToken)
    { await foreach (var item in Responses.Reader.ReadAllAsync(cancellationToken)) yield return item; }
    public ValueTask DisposeAsync() { Responses.Writer.TryComplete(); return ValueTask.CompletedTask; }
}
