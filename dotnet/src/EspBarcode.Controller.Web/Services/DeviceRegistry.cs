namespace EspBarcode.Controller.Web.Services;

/// <summary>All ESP screens currently connected to this browser tab, across possibly-multiple boards.</summary>
public sealed class DeviceRegistry(WebSerialModule module)
{
    private readonly List<DeviceConnection> _devices = [];

    public IReadOnlyList<DeviceConnection> Devices => _devices;
    public event Action? Changed;

    public Task<bool> IsSupportedAsync() => module.IsSupportedAsync();

    /// <summary>Prompts the browser's device picker (requires a user gesture) and connects the chosen port.</summary>
    public async Task<DeviceConnection> ConnectNewAsync(int baudRate = 115200, CancellationToken cancellationToken = default)
    {
        var handle = await module.RequestPortAsync(cancellationToken);
        return await ConnectAsync(handle, baudRate, cancellationToken);
    }

    /// <summary>
    /// Reconnects every port the browser previously granted access to, with no picker prompt — this is
    /// what Full Auto Mode uses to come back up after a page reload without re-asking the user.
    /// </summary>
    public async Task<int> ReconnectAuthorizedAsync(int baudRate = 115200, CancellationToken cancellationToken = default)
    {
        var handles = await module.GetAuthorizedPortsAsync(cancellationToken);
        var connected = 0;
        foreach (var handle in handles)
        {
            if (_devices.Any(d => d.PortId == handle.Id)) continue;
            try
            {
                await ConnectAsync(handle, baudRate, cancellationToken);
                connected++;
            }
            catch (Exception) when (cancellationToken is { IsCancellationRequested: false })
            {
                // Port may be claimed elsewhere or mid-boot; leave it for the next reconnect pass.
            }
        }

        return connected;
    }

    private async Task<DeviceConnection> ConnectAsync(SerialPortHandle handle, int baudRate, CancellationToken cancellationToken)
    {
        var connection = await WebSerialConnection.OpenAsync(module, handle.Id, baudRate, cancellationToken);
        var device = new DeviceConnection(handle.Id, connection, $"Device {_devices.Count + 1}");
        device.Changed += () => Changed?.Invoke();
        _devices.Add(device);
        Changed?.Invoke();

        await device.RefreshAsync(cancellationToken);
        return device;
    }

    public async Task DisconnectAsync(DeviceConnection device)
    {
        _devices.Remove(device);
        await device.DisposeAsync();
        Changed?.Invoke();
    }

    /// <summary>Disconnects and revokes the browser's authorization for this port ("forget this device").</summary>
    public async Task ForgetAsync(DeviceConnection device)
    {
        _devices.Remove(device);
        await device.DisposeAsync();
        await module.ForgetAsync(device.PortId);
        Changed?.Invoke();
    }
}
