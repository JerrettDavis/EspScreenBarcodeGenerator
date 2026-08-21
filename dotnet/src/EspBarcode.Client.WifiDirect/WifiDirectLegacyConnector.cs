using System.Security.Cryptography;
using EspBarcode.Connectivity.Client;
using Windows.Devices.WiFiDirect;
using Windows.Networking.Sockets;

namespace EspBarcode.Client.WifiDirect;

/// <summary>
/// Starts a Windows Wi-Fi Direct legacy group (Windows acts as the access point; the ESP32
/// joins as a normal Wi-Fi station — the ESP32 cannot be a native Wi-Fi Direct/P2P peer, see
/// docs/PROTOCOL_V2.md §12.1), then accepts the display's TCP connection once it joins and
/// dials in.
///
/// Credential flow: this connector generates (or accepts) the SSID/passphrase/port *before*
/// the display can possibly know them. The caller is responsible for getting those three
/// values to the display over some other trusted transport — this codebase does that over
/// BLE via the `device.wifiDirect.configure` command (see docs/PROTOCOL_V2.md §12.6 and
/// `WifiDirectTcpEndpoint::configure` in firmware). <see cref="ConnectAsync"/> does not return
/// until the display has actually joined and connected, so call it only after that bootstrap
/// step has completed (or the display already holds persisted credentials from a prior run).
///
/// Known gap vs. the full §12 design: the TCP listener binds to all local interfaces rather
/// than only the Wi-Fi Direct group's virtual adapter (§12.4 item 7 / §12.7) — WinRT's legacy
/// group-owner API does not expose that adapter's id directly. Scoping the bind is tracked as
/// follow-up work; see docs/PROTOCOL_V2.md §12.
/// </summary>
public sealed class WifiDirectLegacyConnector(string? ssid = null, string? passphrase = null, int port = 47990,
                                              TimeSpan? publisherTimeout = null, TimeSpan? joinTimeout = null)
    : ILinkConnector, IAsyncDisposable
{
    public string Ssid { get; } = ssid ?? GenerateSsid();
    public string Passphrase { get; } = passphrase ?? GeneratePassphrase();
    public int Port { get; } = port;

    private readonly TimeSpan _publisherTimeout = publisherTimeout ?? TimeSpan.FromSeconds(15);
    // Deliberately generous: on a healthy adapter the display should join within a few
    // seconds of being provisioned, but some drivers are slow to actually radiate the group
    // even after WiFiDirectAdvertisementPublisher reports Started (see the class doc's known
    // gap — on at least one qualified adapter, Started never becomes a real broadcast at all).
    private readonly TimeSpan _joinTimeout = joinTimeout ?? TimeSpan.FromSeconds(45);
    private WiFiDirectAdvertisementPublisher? _publisher;
    private StreamSocketListener? _listener;

    public async Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken)
    {
        await StartGroupAsync(cancellationToken);
        StreamSocket socket = await AcceptConnectionAsync(cancellationToken);
        return new WifiDirectTcpLinkConnection(socket);
    }

    private async Task StartGroupAsync(CancellationToken cancellationToken)
    {
        _publisher = new WiFiDirectAdvertisementPublisher();
        _publisher.Advertisement.LegacySettings.IsEnabled = true;
        _publisher.Advertisement.LegacySettings.Ssid = Ssid;
        _publisher.Advertisement.LegacySettings.Passphrase.Password = Passphrase;

        var startedTcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        void OnStatusChanged(WiFiDirectAdvertisementPublisher sender, WiFiDirectAdvertisementPublisherStatusChangedEventArgs args)
        {
            switch (args.Status)
            {
                case WiFiDirectAdvertisementPublisherStatus.Started:
                    startedTcs.TrySetResult();
                    break;
                case WiFiDirectAdvertisementPublisherStatus.Aborted:
                    startedTcs.TrySetException(new InvalidOperationException(
                        $"Wi-Fi Direct legacy group creation aborted (error {args.Error}) — the Wi-Fi adapter/driver may not support legacy group-owner mode, or policy denied it"));
                    break;
            }
        }

        _publisher.StatusChanged += OnStatusChanged;
        try
        {
            _publisher.Start();
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(_publisherTimeout);
            using var registration = timeoutCts.Token.Register(() => startedTcs.TrySetCanceled());
            try
            {
                await startedTcs.Task;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested == false)
            {
                throw new TimeoutException($"Wi-Fi Direct legacy group did not reach Started status within {_publisherTimeout}");
            }
        }
        finally
        {
            _publisher.StatusChanged -= OnStatusChanged;
        }

        // Gap noted in the type doc: not scoped to the group's virtual adapter.
        _listener = new StreamSocketListener();
        await _listener.BindServiceNameAsync(Port.ToString()).AsTask(cancellationToken);
    }

    private async Task<StreamSocket> AcceptConnectionAsync(CancellationToken cancellationToken)
    {
        var connectionTcs = new TaskCompletionSource<StreamSocket>(TaskCreationOptions.RunContinuationsAsynchronously);

        void OnConnectionReceived(StreamSocketListener sender, StreamSocketListenerConnectionReceivedEventArgs args)
            => connectionTcs.TrySetResult(args.Socket);

        _listener!.ConnectionReceived += OnConnectionReceived;
        try
        {
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutCts.CancelAfter(_joinTimeout);
            using var registration = timeoutCts.Token.Register(() => connectionTcs.TrySetCanceled());
            try
            {
                return await connectionTcs.Task;
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested == false)
            {
                throw new TimeoutException(
                    $"no device joined the Wi-Fi Direct group \"{Ssid}\" and connected to port {Port} within {_joinTimeout}. " +
                    "This can mean the display never received (or failed to apply) the provisioned credentials, or — as " +
                    "seen on some adapters (e.g. Intel AX-series) — the Wi-Fi adapter/driver silently does not implement " +
                    "WiFiDirectLegacySettings: WiFiDirectAdvertisementPublisher reports Started without actually " +
                    "radiating an access point. Check Get-NetAdapter / the WLAN-AutoConfig operational event log for " +
                    "driver-level activity to distinguish the two (see docs/PROTOCOL_V2.md §12).");
            }
        }
        finally
        {
            _listener.ConnectionReceived -= OnConnectionReceived;
        }
    }

    public ValueTask DisposeAsync()
    {
        _listener?.Dispose();
        _publisher?.Stop();
        return ValueTask.CompletedTask;
    }

    private static string GenerateSsid() => $"ESBG-{Random.Shared.Next(0, 0x10000):X4}";

    private static string GeneratePassphrase()
    {
        Span<byte> bytes = stackalloc byte[8];
        RandomNumberGenerator.Fill(bytes);
        return Convert.ToHexString(bytes);
    }
}
