using System.Collections.Concurrent;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Client;

public sealed class EspLinkControlSession : IAsyncDisposable
{
    private readonly EspLinkLinkSession _linkSession;
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<(MessageEnvelope Envelope, byte[] Body)>> _pending = new();
    private readonly CancellationTokenSource _cts = new();
    private Task? _receiveLoop;
    private ulong _nextOperationId = 1;

    public EspLinkControlSession(EspLinkLinkSession linkSession) => _linkSession = linkSession;

    public void Start()
    {
        _receiveLoop = Task.Run(async () =>
        {
            await foreach (var message in _linkSession.ReceiveMessagesAsync(_cts.Token))
            {
                if (!MessageEnvelope.TryDecode(message, out var envelope, out var body, out _)) continue;
                if (_pending.TryRemove(envelope.CorrelationId, out var tcs)) tcs.TrySetResult((envelope, body));
            }
        }, _cts.Token);
    }

    public async Task<(MessageEnvelope Envelope, byte[] Body)> SendCommandAsync(
        ServiceId serviceId, byte[] body, uint controlSessionId, TimeSpan timeout, CancellationToken cancellationToken)
    {
        ulong operationId = _nextOperationId++;
        var envelope = new MessageEnvelope
        {
            Kind = MessageKind.Command,
            ServiceId = serviceId,
            CodecId = CodecId.Json,
            ControlSessionId = controlSessionId,
            OperationId = operationId,
        };
        if (!MessageEnvelope.TryEncode(envelope, body, out var message, out var error))
            throw new InvalidOperationException($"failed to encode envelope: {error}");

        var tcs = new TaskCompletionSource<(MessageEnvelope, byte[])>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[operationId] = tcs;

        try
        {
            await _linkSession.SendMessageAsync(message, TrafficClass.Control, CarrierProfileId.StreamStandard, cancellationToken);

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(timeout);
            await using (timeoutCts.Token.Register(() => tcs.TrySetCanceled()))
            {
                return await tcs.Task;
            }
        }
        finally
        {
            _pending.TryRemove(operationId, out _);
        }
    }

    public async ValueTask DisposeAsync()
    {
        _cts.Cancel();
        if (_receiveLoop is not null)
        {
            try { await _receiveLoop; } catch (OperationCanceledException) { }
        }
        await _linkSession.DisposeAsync();
        _cts.Dispose();
    }
}
