using System.Collections.Concurrent;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Client;

public sealed class EspLinkControlSession : IAsyncDisposable
{
    private readonly IMessageLinkSession _linkSession;
    private readonly CarrierProfileId _profileId;
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<(MessageEnvelope Envelope, byte[] Body)>> _pending = new();
    private readonly CancellationTokenSource _cts = new();
    private Task? _receiveLoop;
    private ulong _nextOperationId = 1;

    public EspLinkControlSession(IMessageLinkSession linkSession, CarrierProfileId profileId = CarrierProfileId.StreamStandard)
        => (_linkSession, _profileId) = (linkSession, profileId);

    public void Start()
    {
        _receiveLoop = Task.Run(async () =>
        {
            try
            {
                await foreach (var message in _linkSession.ReceiveMessagesAsync(_cts.Token))
                {
                    if (!MessageEnvelope.TryDecode(message, out var envelope, out var body, out _)) continue;
                    if (_pending.TryRemove(envelope.CorrelationId, out var tcs)) tcs.TrySetResult((envelope, body));
                }
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                // Fail every still-pending request instead of leaving it to hang until its own
                // timeout — a dead receive loop otherwise looks identical to "no response yet".
                foreach (var pending in _pending.Values) pending.TrySetException(ex);
            }
        }, _cts.Token);
    }

    public async Task<(MessageEnvelope Envelope, byte[] Body)> SendCommandAsync(
        ServiceId serviceId, byte[] body, uint controlSessionId, TimeSpan timeout, CancellationToken cancellationToken,
        ushort routeId = 0)
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
            await _linkSession.SendMessageAsync(message, TrafficClass.Control, _profileId, cancellationToken, routeId);

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
