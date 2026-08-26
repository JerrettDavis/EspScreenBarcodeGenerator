using EspBarcode.Protocol;
namespace EspBarcode.Connectivity.Client;
public interface IMessageLinkSession : IAsyncDisposable
{
    Task SendMessageAsync(byte[] layer3Message, TrafficClass trafficClass, CarrierProfileId profileId,
        CancellationToken cancellationToken, ushort routeId = 0);
    IAsyncEnumerable<byte[]> ReceiveMessagesAsync(CancellationToken cancellationToken);
}
