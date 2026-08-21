using EspBarcode.Connectivity.Client;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Tests;

public class DatagramLinkSessionTests
{
    [Fact]
    public async Task SendMessage_IsReceivedIntactOnTheOtherEndOfALoopback_NoCobsWrapping()
    {
        var (left, right) = InMemoryDuplexConnection.CreatePair();
        await using var leftSession = new EspLinkDatagramLinkSession(left, linkSessionId: 1, maxFrameBytes: 200);
        await using var rightSession = new EspLinkDatagramLinkSession(right, linkSessionId: 1, maxFrameBytes: 200);

        var body = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.ping\",\"body\":{}}"u8.ToArray();
        var envelope = new MessageEnvelope { OperationId = 1 };
        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out _));

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var receiveTask = rightSession.ReceiveMessagesAsync(cts.Token).GetAsyncEnumerator();

        await leftSession.SendMessageAsync(message, TrafficClass.Control, CarrierProfileId.BleGattV1, cts.Token);

        Assert.True(await receiveTask.MoveNextAsync());
        Assert.Equal(message, receiveTask.Current);
    }

    [Fact]
    public async Task SendMessage_LargerThanOneFrame_SplitsAndReassembles()
    {
        var (left, right) = InMemoryDuplexConnection.CreatePair();
        const int maxFrameBytes = 64;  // small ceiling to force multi-fragment
        await using var leftSession = new EspLinkDatagramLinkSession(left, linkSessionId: 7, maxFrameBytes: maxFrameBytes);
        await using var rightSession = new EspLinkDatagramLinkSession(right, linkSessionId: 7, maxFrameBytes: maxFrameBytes);

        var body = new byte[300];
        for (int i = 0; i < body.Length; i++) body[i] = (byte)i;
        var envelope = new MessageEnvelope { OperationId = 42 };
        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out _));

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var receiveTask = rightSession.ReceiveMessagesAsync(cts.Token).GetAsyncEnumerator();

        await leftSession.SendMessageAsync(message, TrafficClass.Control, CarrierProfileId.BleGattV1, cts.Token);

        Assert.True(await receiveTask.MoveNextAsync());
        Assert.Equal(message, receiveTask.Current);
    }

    [Fact]
    public async Task ControlSession_CorrelatesRequestAndResponseAcrossADatagramLoopback()
    {
        var (clientConn, deviceConn) = InMemoryDuplexConnection.CreatePair();
        await using var clientLink = new EspLinkDatagramLinkSession(clientConn, linkSessionId: 1, maxFrameBytes: 200);
        await using var deviceLink = new EspLinkDatagramLinkSession(deviceConn, linkSessionId: 1, maxFrameBytes: 200);

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

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
                await deviceLink.SendMessageAsync(responseMessage, TrafficClass.Control, CarrierProfileId.BleGattV1, cts.Token);
                break;
            }
        }, cts.Token);

        var requestBody = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.hello\",\"body\":{}}"u8.ToArray();
        var requestEnvelope = new MessageEnvelope { OperationId = 1 };
        Assert.True(MessageEnvelope.TryEncode(requestEnvelope, requestBody, out var requestMessage, out _));

        var receiveTask = clientLink.ReceiveMessagesAsync(cts.Token).GetAsyncEnumerator();
        await clientLink.SendMessageAsync(requestMessage, TrafficClass.Control, CarrierProfileId.BleGattV1, cts.Token);

        Assert.True(await receiveTask.MoveNextAsync());
        Assert.True(MessageEnvelope.TryDecode(receiveTask.Current, out var responseEnvelope, out _, out _));
        Assert.Equal(MessageKind.Result, responseEnvelope.Kind);
        Assert.Equal(requestEnvelope.OperationId, responseEnvelope.CorrelationId);
        await deviceTask;
    }
}
