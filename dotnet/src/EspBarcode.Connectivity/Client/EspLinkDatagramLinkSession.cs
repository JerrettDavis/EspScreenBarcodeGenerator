using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Client;

/// <summary>
/// Link session for datagram carriers (BLE GATT, a future ESP-NOW gateway link) where each
/// <see cref="ILinkConnection.WriteAsync"/>/<see cref="ILinkConnection.ReadAsync"/> call is
/// already exactly one carrier message — no COBS delimiting, unlike <see cref="EspLinkLinkSession"/>,
/// which assumes a byte stream. A connection used here MUST return exactly one hop frame's
/// bytes per <see cref="ILinkConnection.ReadAsync"/> call (never partial, never merged); the
/// firmware side of every datagram carrier (`EspNowEndpoint`, `BleGattEndpoint`) upholds the
/// mirror-image contract: one write/notify per hop frame.
/// </summary>
public sealed class EspLinkDatagramLinkSession(ILinkConnection connection, uint linkSessionId, int maxFrameBytes)
    : IMessageLinkSession
{
    private readonly FrameAssembler _assembler = new();
    private uint _linkMessageCounter = 1;

    public async Task SendMessageAsync(byte[] layer3Message, TrafficClass trafficClass, CarrierProfileId profileId,
                                       CancellationToken cancellationToken, ushort routeId = 0)
    {
        int maxPayload = maxFrameBytes - HopFrameHeader.Overhead;
        if (maxPayload <= 0)
            throw new InvalidOperationException($"maxFrameBytes ({maxFrameBytes}) does not leave room for any payload past the {HopFrameHeader.Overhead}-byte hop frame overhead");

        int fragmentCount = (layer3Message.Length + maxPayload - 1) / maxPayload;
        if (fragmentCount == 0) fragmentCount = 1;  // a zero-length message still needs one (empty-payload) frame
        if (fragmentCount > ushort.MaxValue)
            throw new InvalidOperationException($"message ({layer3Message.Length} bytes) needs more fragments than fit in a ushort at {maxFrameBytes}-byte frames");

        uint linkMessageId = _linkMessageCounter++;
        for (int i = 0; i < fragmentCount; i++)
        {
            int offset = i * maxPayload;
            int length = Math.Min(maxPayload, layer3Message.Length - offset);
            var header = new HopFrameHeader
            {
                TrafficClass = trafficClass,
                ProfileId = profileId,
                RouteId = routeId,
                LinkSessionId = linkSessionId,
                LinkMessageId = linkMessageId,
                FragmentIndex = (ushort)i,
                FragmentCount = (ushort)fragmentCount,
            };
            if (!HopFrameHeader.TryEncode(header, layer3Message.AsSpan(offset, length), out var frame, out var error))
                throw new InvalidOperationException($"failed to encode hop frame fragment {i}/{fragmentCount}: {error}");

            await connection.WriteAsync(frame, cancellationToken);
        }
    }

    public async IAsyncEnumerable<byte[]> ReceiveMessagesAsync(
        [System.Runtime.CompilerServices.EnumeratorCancellation] CancellationToken cancellationToken)
    {
        var buffer = new byte[maxFrameBytes];
        while (!cancellationToken.IsCancellationRequested)
        {
            int read = await connection.ReadAsync(buffer, cancellationToken);
            if (read == 0) yield break;

            if (!HopFrameHeader.TryDecode(buffer.AsSpan(0, read), out var header, out var payload, out _)) continue;
            var outcome = _assembler.AddFragment(header, payload, out var assembled);
            if (outcome == AssemblyOutcome.Complete) yield return assembled;
        }
    }

    public ValueTask DisposeAsync() => connection.DisposeAsync();
}
