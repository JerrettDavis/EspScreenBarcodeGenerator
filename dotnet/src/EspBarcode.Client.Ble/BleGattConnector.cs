using System.Runtime.InteropServices.WindowsRuntime;
using EspBarcode.Connectivity.Client;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace EspBarcode.Client.Ble;

/// <summary>
/// Scans for, connects to, and opens the EspLink v2 GATT session on the display's
/// `BleGattEndpoint` (firmware `src/BleGattEndpoint.cpp`). UUIDs here MUST match the
/// firmware's `kServiceUuid`/`kCommandCharUuid`/`kEventCharUuid` — see docs/PROTOCOL_V2.md §5.
/// </summary>
public sealed class BleGattConnector(string? deviceNameFilter = "EspScreenBarcodeGenerator", TimeSpan? scanTimeout = null)
    : ILinkConnector
{
    public static readonly Guid ServiceUuid = Guid.Parse("6f6d7501-2e73-4a1a-9d3f-1c9b6f4e5a01");
    public static readonly Guid CommandCharacteristicUuid = Guid.Parse("6f6d7502-2e73-4a1a-9d3f-1c9b6f4e5a01");
    public static readonly Guid EventCharacteristicUuid = Guid.Parse("6f6d7503-2e73-4a1a-9d3f-1c9b6f4e5a01");

    private readonly TimeSpan _scanTimeout = scanTimeout ?? TimeSpan.FromSeconds(10);

    public async Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken)
    {
        ulong address = await ScanForDeviceAsync(cancellationToken);

        var device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
            ?? throw new InvalidOperationException($"failed to open a BLE connection to address {address:X12}");

        var servicesResult = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
        if (servicesResult.Status != GattCommunicationStatus.Success || servicesResult.Services.Count == 0)
            throw new InvalidOperationException($"EspLink v2 BLE service not found on device (status {servicesResult.Status})");
        var service = servicesResult.Services[0];

        var commandResult = await service.GetCharacteristicsForUuidAsync(CommandCharacteristicUuid, BluetoothCacheMode.Uncached);
        if (commandResult.Status != GattCommunicationStatus.Success || commandResult.Characteristics.Count == 0)
            throw new InvalidOperationException($"BLE command characteristic not found (status {commandResult.Status})");

        var eventResult = await service.GetCharacteristicsForUuidAsync(EventCharacteristicUuid, BluetoothCacheMode.Uncached);
        if (eventResult.Status != GattCommunicationStatus.Success || eventResult.Characteristics.Count == 0)
            throw new InvalidOperationException($"BLE event characteristic not found (status {eventResult.Status})");

        var connection = new BleGattLinkConnection(device, commandResult.Characteristics[0], eventResult.Characteristics[0]);
        await connection.SubscribeAsync();
        return connection;
    }

    private async Task<ulong> ScanForDeviceAsync(CancellationToken cancellationToken)
    {
        var watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        var found = new TaskCompletionSource<ulong>(TaskCreationOptions.RunContinuationsAsynchronously);

        // The service UUID and the local name normally arrive in separate advertisement
        // PDUs (primary advertisement vs. scan response, each capped at 31 bytes) — a
        // single Received event carries one or the other, essentially never both. So this
        // matches on either signal alone rather than requiring both in the same event.
        void OnReceived(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementReceivedEventArgs args)
        {
            bool matchesService = args.Advertisement.ServiceUuids.Contains(ServiceUuid);
            bool matchesName = deviceNameFilter is not null
                && string.Equals(args.Advertisement.LocalName, deviceNameFilter, StringComparison.Ordinal);
            if (matchesService || matchesName) found.TrySetResult(args.BluetoothAddress);
        }

        watcher.Received += OnReceived;
        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutCts.CancelAfter(_scanTimeout);
        using var registration = timeoutCts.Token.Register(() => found.TrySetCanceled());

        watcher.Start();
        try
        {
            return await found.Task;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested == false)
        {
            throw new TimeoutException(
                $"no BLE advertisement matching service {ServiceUuid} / name \"{deviceNameFilter}\" seen within {_scanTimeout}");
        }
        finally
        {
            watcher.Received -= OnReceived;
            watcher.Stop();
        }
    }
}
