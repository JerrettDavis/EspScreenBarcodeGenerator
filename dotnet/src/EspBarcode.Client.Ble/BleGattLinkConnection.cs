using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Channels;
using EspBarcode.Connectivity.Client;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace EspBarcode.Client.Ble;

/// <summary>
/// One connected EspLink v2 BLE GATT session: writes go to the command characteristic,
/// notifications from the event characteristic are queued and handed out one-per-<see
/// cref="ReadAsync"/> call — <see cref="EspBarcode.Connectivity.Client.EspLinkDatagramLinkSession"/>
/// relies on that one-message-per-read contract (see its remarks).
/// </summary>
public sealed class BleGattLinkConnection : ILinkConnection
{
    private readonly BluetoothLEDevice _device;
    private readonly GattCharacteristic _commandCharacteristic;
    private readonly GattCharacteristic _eventCharacteristic;
    private readonly Channel<byte[]> _incoming = Channel.CreateUnbounded<byte[]>();

    internal BleGattLinkConnection(BluetoothLEDevice device, GattCharacteristic commandCharacteristic,
                                   GattCharacteristic eventCharacteristic)
    {
        _device = device;
        _commandCharacteristic = commandCharacteristic;
        _eventCharacteristic = eventCharacteristic;
        _eventCharacteristic.ValueChanged += OnValueChanged;
    }

    internal async Task SubscribeAsync()
    {
        var status = await _eventCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (status != GattCommunicationStatus.Success)
            throw new InvalidOperationException($"failed to subscribe to BLE event notifications: {status}");
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
        => _incoming.Writer.TryWrite(args.CharacteristicValue.ToArray());

    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
    {
        var status = await _commandCharacteristic.WriteValueAsync(bytes.ToArray().AsBuffer(), GattWriteOption.WriteWithResponse)
            .AsTask(cancellationToken);
        if (status != GattCommunicationStatus.Success)
            throw new InvalidOperationException($"BLE characteristic write failed: {status}");
    }

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        byte[] message = await _incoming.Reader.ReadAsync(cancellationToken);
        if (message.Length > buffer.Length)
            throw new InvalidOperationException(
                $"received a {message.Length}-byte BLE notification but the read buffer is only {buffer.Length} bytes");
        message.CopyTo(buffer);
        return message.Length;
    }

    public ValueTask DisposeAsync()
    {
        _eventCharacteristic.ValueChanged -= OnValueChanged;
        _device.Dispose();
        _incoming.Writer.TryComplete();
        return ValueTask.CompletedTask;
    }
}
