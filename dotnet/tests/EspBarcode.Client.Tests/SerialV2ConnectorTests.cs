using EspBarcode.Client.TransportV2;

namespace EspBarcode.Client.Tests;

public class SerialV2ConnectorTests
{
    [Fact]
    public void RequestUpgrade_SendsTheUpgradeCommand()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"upgrade","message":"switching to EspLink v2 COBS framing"}""");

        UpgradeHandshake.RequestUpgrade(transport);

        Assert.Single(transport.WrittenLines);
        Assert.Contains("\"cmd\":\"upgrade\"", transport.WrittenLines[0]);
    }

    [Fact]
    public void RequestUpgrade_PropagatesADeviceRejection()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":false,"cmd":"upgrade","error":{"code":"unknown_command","message":"unsupported command"}}""");

        var exception = Assert.Throws<EspBarcodeProtocolException>(() => UpgradeHandshake.RequestUpgrade(transport));
        Assert.Equal("unknown_command", exception.Code);
    }
}
