using EspBarcode.Controller.Web.Models;

namespace EspBarcode.Controller.Web.Services;

public enum DeviceMode { Client, GatewayRelay }

/// <summary>One connected ESP screen: its transport, its v1 client, and (once switched) its v2 gateway link.</summary>
public sealed class DeviceConnection : IAsyncDisposable
{
    public string Id { get; } = Guid.NewGuid().ToString("n");
    public string PortId { get; }
    public string Nickname { get; set; }
    public DeviceMode Mode { get; private set; } = DeviceMode.Client;
    public WebSerialConnection Connection { get; }
    public EspDeviceClient Client { get; }
    public GatewayLinkClient? GatewayLink { get; private set; }
    public uint GatewayControlSessionId { get; private set; }
    public DeviceInfo? LastHello { get; private set; }
    public StatusInfo? LastStatus { get; private set; }
    public string? LastError { get; private set; }
    public DateTimeOffset? LastSeenUtc { get; private set; }
    public bool IsConnected { get; private set; } = true;

    public event Action? Changed;

    internal DeviceConnection(string portId, WebSerialConnection connection, string nickname)
    {
        PortId = portId;
        Connection = connection;
        Client = new EspDeviceClient(connection);
        Nickname = nickname;
        connection.Closed += HandleConnectionClosed;
    }

    private void HandleConnectionClosed()
    {
        IsConnected = false;
        Changed?.Invoke();
    }

    public async Task RefreshAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            LastHello ??= await Client.HelloAsync(cancellationToken);
            LastStatus = await Client.StatusAsync(cancellationToken);
            LastSeenUtc = DateTimeOffset.UtcNow;
            LastError = null;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            LastError = ex.Message;
        }

        Changed?.Invoke();
    }

    /// <summary>
    /// Switches this board into a USB&lt;-&gt;ESP-NOW relay and immediately negotiates a v2 control
    /// session over the same wire so commands can reach the ESP-NOW-connected peer on the other side.
    /// One-way per docs/PROTOCOL_V2.md §"ESP-NOW USB gateway" — only a device reboot reverts it.
    /// </summary>
    public async Task EnterGatewayModeAsync(CancellationToken cancellationToken = default)
    {
        await Client.EnterGatewayModeAsync(cancellationToken);
        Mode = DeviceMode.GatewayRelay;
        GatewayLink = new GatewayLinkClient(Connection);
        var (_, _, controlSessionId) = await GatewayLink.HelloAsync(cancellationToken: cancellationToken);
        GatewayControlSessionId = controlSessionId;
        Changed?.Invoke();
    }

    public async ValueTask DisposeAsync()
    {
        if (GatewayLink is not null) await GatewayLink.DisposeAsync();
        await Connection.DisposeAsync();
    }
}
