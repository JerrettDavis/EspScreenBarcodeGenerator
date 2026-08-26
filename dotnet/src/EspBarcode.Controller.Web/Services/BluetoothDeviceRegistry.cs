namespace EspBarcode.Controller.Web.Services;
public sealed class BluetoothDevice(string id, string name, GatewayLinkClient client, uint sessionId, string firmware) : IAsyncDisposable
{
    public string Id { get; } = id; public string Name { get; set; } = name; public GatewayLinkClient Client { get; } = client;
    public uint ControlSessionId { get; } = sessionId; public string Firmware { get; } = firmware;
    public ValueTask DisposeAsync() => Client.DisposeAsync();
}
public sealed class BluetoothDeviceRegistry(WebBluetoothModule module)
{
    private readonly List<BluetoothDevice> _devices = [];
    public IReadOnlyList<BluetoothDevice> Devices => _devices; public event Action? Changed;
    public Task<bool> IsSupportedAsync() => module.IsSupportedAsync();
    public async Task<BluetoothDevice> ConnectAsync(CancellationToken ct = default)
    {
        var connection = await WebBluetoothConnection.OpenAsync(module, ct);
        var client = new GatewayLinkClient(connection, (uint)Random.Shared.Next(1, int.MaxValue), connection.MaxFrameBytes);
        try { var hello = await client.HelloAsync(cancellationToken: ct); var device = new BluetoothDevice(connection.Id, connection.Name, client, hello.ControlSessionId, hello.Firmware); _devices.Add(device); Changed?.Invoke(); return device; }
        catch { await client.DisposeAsync(); throw; }
    }
    public async Task DisconnectAsync(BluetoothDevice device) { _devices.Remove(device); await device.DisposeAsync(); Changed?.Invoke(); }
}
