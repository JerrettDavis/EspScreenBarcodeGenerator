using EspBarcode.Controller.Web.Models;

namespace EspBarcode.Controller.Web.Services;

public enum GenerationTargetKind { Wired, GatewayDirect, GatewayPeer, Bluetooth }

/// <summary>
/// One thing a barcode can be sent to, regardless of which transport it's actually reachable
/// over — built by <see cref="GenerationTargetProvider"/> from <see cref="DeviceRegistry"/> and
/// <see cref="BluetoothDeviceRegistry"/> so Generator.razor can offer one flat "Target Devices"
/// list instead of branching per connection kind.
/// </summary>
public sealed record GenerationTarget(
    string Id,
    string DisplayName,
    GenerationTargetKind Kind,
    Func<GenerateOptions, CancellationToken, Task<GenerateResult>> SendAsync,
    Func<CancellationToken, Task<DownloadedMatrix>>? DownloadPreviewAsync);

/// <summary>
/// Builds <see cref="GenerationTarget"/>s from the two device registries. <c>DownloadPreviewAsync</c>
/// is populated only for <see cref="GenerationTargetKind.Wired"/> targets today — only
/// <see cref="EspDeviceClient"/> (v1 protocol) has a download-current-matrix command;
/// <see cref="GatewayLinkClient"/> (v2, used by Bluetooth and gateway-relay targets) does not yet.
/// </summary>
public sealed class GenerationTargetProvider(DeviceRegistry wired, BluetoothDeviceRegistry bluetooth)
{
    /// <summary>Wired Client-mode devices and Bluetooth devices — always available, no round trip.</summary>
    public IReadOnlyList<GenerationTarget> GetImmediateTargets()
    {
        var targets = new List<GenerationTarget>();

        foreach (var device in wired.Devices.Where(d => d.IsConnected && d.Mode == DeviceMode.Client))
        {
            targets.Add(new GenerationTarget(
                $"wired:{device.Id}", device.Nickname, GenerationTargetKind.Wired,
                (options, ct) => device.Client.GenerateAsync(options, ct),
                ct => device.Client.DownloadCurrentMatrixAsync(ct: ct)));
        }

        foreach (var device in bluetooth.Devices)
        {
            targets.Add(new GenerationTarget(
                $"ble:{device.Id}", device.Name, GenerationTargetKind.Bluetooth,
                (options, ct) => device.Client.GenerateAsync(options, device.ControlSessionId, cancellationToken: ct),
                DownloadPreviewAsync: null));
        }

        return targets;
    }

    /// <summary>
    /// A wired Gateway-relay device itself, addressed directly (routeId 0) — the same target
    /// Wireless.razor's "wired:{id}" send path already used for a connected Gateway-relay board;
    /// semantics unchanged, just centralized here.
    /// </summary>
    public GenerationTarget GetGatewayDirectTarget(DeviceConnection gateway)
    {
        var link = gateway.GatewayLink
            ?? throw new InvalidOperationException($"Device '{gateway.Nickname}' is not in gateway mode.");

        return new GenerationTarget(
            $"wired:{gateway.Id}", gateway.Nickname, GenerationTargetKind.GatewayDirect,
            (options, ct) => link.GenerateAsync(options, gateway.GatewayControlSessionId, cancellationToken: ct),
            DownloadPreviewAsync: null);
    }

    /// <summary>
    /// One gateway's trusted ESP-NOW peers, addressed by routeId. The caller decides when to fetch
    /// <paramref name="peers"/> (an explicit <c>GatewayLinkClient.ListTrustedPeersAsync</c> round trip) —
    /// this only turns already-fetched peers into targets, matching Wireless.razor's
    /// "Refresh Paired Gateway Screens" button today.
    /// </summary>
    public IReadOnlyList<GenerationTarget> BuildPeerTargets(DeviceConnection gateway, IReadOnlyList<TrustedPeer> peers)
    {
        var link = gateway.GatewayLink
            ?? throw new InvalidOperationException($"Device '{gateway.Nickname}' is not in gateway mode.");

        return peers.Select(peer => new GenerationTarget(
            $"peer:{gateway.Id}:{peer.RouteId}",
            string.IsNullOrWhiteSpace(peer.Label) ? peer.Fingerprint : peer.Label,
            GenerationTargetKind.GatewayPeer,
            (options, ct) => link.GenerateAsync(
                options, gateway.GatewayControlSessionId, cancellationToken: ct, routeId: checked((ushort)peer.RouteId)),
            DownloadPreviewAsync: null)).ToArray();
    }
}
