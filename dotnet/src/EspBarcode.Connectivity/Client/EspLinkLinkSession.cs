using System.Runtime.CompilerServices;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Client;

public sealed class EspLinkLinkSession(ILinkConnection connection, uint linkSessionId) : IMessageLinkSession
{
    private readonly FrameAssembler _assembler = new();
    private readonly List<byte> _rxBlock = [];
    private uint _linkMessageCounter = 1;

    public async Task SendMessageAsync(byte[] layer3Message, TrafficClass trafficClass, CarrierProfileId profileId,
                                       CancellationToken cancellationToken, ushort routeId = 0)
    {
        var header = new HopFrameHeader
        {
                TrafficClass = trafficClass,
                ProfileId = profileId,
                RouteId = routeId,
            LinkSessionId = linkSessionId,
            LinkMessageId = _linkMessageCounter++,
            FragmentIndex = 0,
            FragmentCount = 1,
        };
        if (!HopFrameHeader.TryEncode(header, layer3Message, out var frame, out var error))
            throw new InvalidOperationException($"failed to encode hop frame: {error}");

        var cobs = Cobs.Encode(frame);
        var withDelimiter = new byte[cobs.Length + 1];
        cobs.CopyTo(withDelimiter, 0);
        await connection.WriteAsync(withDelimiter, cancellationToken);
    }

    public async IAsyncEnumerable<byte[]> ReceiveMessagesAsync([EnumeratorCancellation] CancellationToken cancellationToken)
    {
        var buffer = new byte[512];
        while (!cancellationToken.IsCancellationRequested)
        {
            int read = await connection.ReadAsync(buffer, cancellationToken);
            if (read == 0) yield break;

            for (int i = 0; i < read; i++)
            {
                byte b = buffer[i];
                if (b == 0x00)
                {
                    if (_rxBlock.Count > 0 && TryProcessBlock([.. _rxBlock], out var message)) yield return message;
                    _rxBlock.Clear();
                    continue;
                }
                if (_rxBlock.Count < 2048) _rxBlock.Add(b);
            }
        }
    }

    private bool TryProcessBlock(byte[] block, out byte[] message)
    {
        message = [];
        if (!Cobs.TryDecode(block, out var raw)) return false;
        if (!HopFrameHeader.TryDecode(raw, out var header, out var payload, out _)) return false;
        var outcome = _assembler.AddFragment(header, payload, out var assembled);
        if (outcome != AssemblyOutcome.Complete) return false;
        message = assembled;
        return true;
    }

    public ValueTask DisposeAsync() => connection.DisposeAsync();
}
