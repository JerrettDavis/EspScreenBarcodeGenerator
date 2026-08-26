using System.IO.Ports;
using System.Text;

namespace EspBarcode.Client.Transport;

/// <summary>Real hardware transport backed by <see cref="SerialPort"/>.</summary>
public sealed class SerialPortTransport : IEspBarcodeTransport
{
    private readonly SerialPort _port;

    public SerialPortTransport(string portName, int baudRate = 115200, TimeSpan? pollTimeout = null)
    {
        _port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
        {
            Encoding = Encoding.UTF8,
            NewLine = "\n",
            ReadTimeout = (int)(pollTimeout ?? TimeSpan.FromMilliseconds(150)).TotalMilliseconds,
            WriteTimeout = 3000,
        };
        _port.Open();

        // The classic Arduino/ESP32 auto-reset circuit reads DTR and RTS
        // together; leaving RTS at .NET's default (false) while DTR is true
        // asserts the two lines asymmetrically and can drive the board into
        // the ROM bootloader (GPIO0 held low) instead of a clean app reset —
        // esptool still talks to it there, but the firmware never runs.
        // Asserting both, matching pyserial's default open behavior, resets
        // cleanly into the app.
        _port.DtrEnable = true;
        _port.RtsEnable = true;

        // Give the firmware time to reboot and mount its filesystem before
        // the caller's first request lands, and drop whatever boot chatter
        // arrived meanwhile.
        // The current full firmware (BLE + ESP-NOW + LittleFS + TFT) needs just over 1.5s
        // on the two production CH340 boards measured during the mobile-controller hardware
        // acceptance pass. A command sent at the old 1.5s boundary was silently lost on both
        // cold boots, making the one-way gateway handshake appear to fail. Keep margin here:
        // unlike a retry, waiting before the first command cannot accidentally send NDJSON
        // after the device has already switched to COBS framing.
        Thread.Sleep(2500);
        _port.DiscardInBuffer();
    }

    /// <summary>
    /// The underlying open <see cref="SerialPort"/>, for callers (like <c>SerialV2Connector</c>)
    /// that need to hand the same physical connection to a different framing layer without
    /// closing and reopening it — reopening re-triggers the DTR/RTS reset above.
    /// </summary>
    public SerialPort UnderlyingPort => _port;

    public void WriteLine(string line) => _port.WriteLine(line);

    public string? ReadLine()
    {
        try
        {
            return _port.ReadLine();
        }
        catch (TimeoutException)
        {
            return null;
        }
    }

    public void Dispose()
    {
        if (_port.IsOpen) _port.Close();
        _port.Dispose();
    }
}
