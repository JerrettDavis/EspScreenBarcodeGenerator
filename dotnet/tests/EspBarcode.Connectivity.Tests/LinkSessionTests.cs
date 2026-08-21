using EspBarcode.Connectivity.Client;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Tests;

public class LinkSessionTests
{
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
