using EspBarcode.Client.Transport;

namespace EspBarcode.Client.Tests;

/// <summary>
/// Scripted transport for exercising <see cref="EspBarcodeClient"/> without
/// real hardware, mirroring how tests/test_host_tool.py drives the Python
/// client against canned protocol traffic.
/// </summary>
internal sealed class FakeTransport : IEspBarcodeTransport
{
    private readonly Queue<string?> _incoming = new();

    public List<string> WrittenLines { get; } = [];

    /// <summary>Queues a line to hand back from the next <see cref="ReadLine"/> call.</summary>
    public void Enqueue(string line) => _incoming.Enqueue(line);

    /// <summary>Queues a simulated poll timeout (the real transport returns null the same way).</summary>
    public void EnqueueTimeout() => _incoming.Enqueue(null);

    public void WriteLine(string line) => WrittenLines.Add(line);

    public string? ReadLine()
    {
        if (_incoming.Count == 0)
        {
            // Nothing scripted: behave like a transport still waiting on its poll
            // timeout so callers relying on their own overall deadline don't spin.
            Thread.Sleep(1);
            return null;
        }
        return _incoming.Dequeue();
    }

    public void Dispose()
    {
    }
}
