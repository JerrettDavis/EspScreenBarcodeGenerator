using System.Linq;
using System.Text;
using System.Text.Json.Nodes;
using EspBarcode.Connectivity.Client;
using EspBarcode.Controller.Web.Models;
using EspBarcode.Protocol;

namespace EspBarcode.Controller.Web.Services;

/// <summary>
/// Speaks the EspLink v2 subset a gateway-mode board relays to a second, ESP-NOW-connected display
/// (docs/PROTOCOL_V2.md §7) — <c>system.hello</c> and <c>barcode.generate</c> are the two commands
/// exercised here, matching this session's hardware-validated .NET↔gateway↔ESP-NOW↔display round trip.
/// Wraps the same portable <see cref="EspLinkLinkSession"/>/<see cref="EspLinkControlSession"/> stack the
/// desktop BLE/Wi-Fi-Direct/serial connectors use, over a <see cref="WebSerialConnection"/> carrier.
/// </summary>
public sealed class GatewayLinkClient : IAsyncDisposable
{
    private const string Schema = "esbg.control/2.0";
    private readonly EspLinkLinkSession _linkSession;
    private readonly EspLinkControlSession _controlSession;

    public GatewayLinkClient(WebSerialConnection connection, uint linkSessionId = 1)
    {
        _linkSession = new EspLinkLinkSession(connection, linkSessionId);
        _controlSession = new EspLinkControlSession(_linkSession);
        _controlSession.Start();
    }

    private Task<JsonObject> SendAsync(
        string name, JsonObject? body, uint controlSessionId, TimeSpan timeout, CancellationToken cancellationToken)
        => SendAsync(ServiceId.System, name, body, controlSessionId, timeout, cancellationToken);

    private async Task<JsonObject> SendAsync(
        ServiceId serviceId, string name, JsonObject? body, uint controlSessionId, TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var wrapper = new JsonObject { ["schema"] = Schema, ["name"] = name, ["body"] = body ?? new JsonObject() };
        var bytes = Encoding.UTF8.GetBytes(wrapper.ToJsonString());
        var (envelope, respBytes) = await _controlSession.SendCommandAsync(
            serviceId, bytes, controlSessionId, timeout, cancellationToken);

        var respText = Encoding.UTF8.GetString(respBytes);
        if (JsonNode.Parse(respText) is not JsonObject respWrapper)
            throw new InvalidOperationException($"malformed EspLink v2 response: {respText}");

        var errorObj = respWrapper["error"] as JsonObject;
        if (envelope.Kind == MessageKind.Error || errorObj is not null)
        {
            var code = errorObj?["code"]?.GetValue<string>() ?? "device_error";
            var message = errorObj?["message"]?.GetValue<string>() ?? respText;
            throw new EspBarcodeProtocolException(code, message);
        }

        return respWrapper["body"]?.AsObject() ?? [];
    }

    /// <summary>
    /// Negotiates a control session with the far side of the relay (<c>system.hello</c> →
    /// <c>system.welcome</c>). The returned <c>ControlSessionId</c> must be supplied to every
    /// subsequent v2 call on this link.
    /// </summary>
    public async Task<(string DeviceId, string Firmware, uint ControlSessionId)> HelloAsync(
        TimeSpan? timeout = null, CancellationToken cancellationToken = default)
    {
        var body = await SendAsync("system.hello", null, controlSessionId: 0, timeout ?? TimeSpan.FromSeconds(5), cancellationToken);
        return (
            body["deviceId"]!.GetValue<string>(),
            body["firmware"]!.GetValue<string>(),
            body["controlSessionId"]!.GetValue<uint>());
    }

    public async Task<GenerateResult> GenerateAsync(
        GenerateOptions options, uint controlSessionId, TimeSpan? timeout = null, CancellationToken cancellationToken = default)
    {
        var body = new JsonObject
        {
            ["type"] = options.Type.ToWireValue(),
            ["data"] = options.Data,
            ["display"] = options.Display,
            ["ecc"] = options.Ecc,
            ["rotation"] = options.Rotation,
            ["quiet"] = options.Quiet,
            ["min_module"] = options.MinModule,
            ["rect"] = options.Rectangular,
            ["invert"] = options.Invert,
            ["checksum"] = options.Checksum,
            ["qr_min_version"] = options.QrMinVersion,
            ["qr_max_version"] = options.QrMaxVersion,
            ["aztec_security"] = options.AztecSecurity,
            ["aztec_layers"] = options.AztecLayers,
        };

        var r = await SendAsync("barcode.generate", body, controlSessionId, timeout ?? TimeSpan.FromSeconds(8), cancellationToken);
        return new GenerateResult(
            r["type"]!.GetValue<string>(), r["width"]!.GetValue<int>(), r["height"]!.GetValue<int>(),
            r["linear"]!.GetValue<bool>(), r["quiet"]!.GetValue<int>(), r["displayed"]!.GetValue<bool>(),
            r["normalized_data"]!.GetValue<string>());
    }

    /// <summary>
    /// Asks the gateway board itself (not the far-side relayed display) for its live ESP-NOW
    /// peer list — <c>gateway.peers.list</c>, answered locally by GatewayRelay without crossing
    /// the relay (see <c>GatewayRelay::handleGatewayServiceFromUsb</c>).
    /// </summary>
    public async Task<IReadOnlyList<GatewayPeer>> ListPeersAsync(
        TimeSpan? timeout = null, CancellationToken cancellationToken = default)
    {
        var body = await SendAsync(
            ServiceId.Gateway, "gateway.peers.list", null, controlSessionId: 0,
            timeout ?? TimeSpan.FromSeconds(5), cancellationToken);
        var peers = body["peers"]?.AsArray() ?? [];
        return peers.Select(p =>
        {
            var o = p!.AsObject();
            return new GatewayPeer(
                o["mac"]!.GetValue<string>(), o["last_seen_ms_ago"]!.GetValue<long>(),
                o["via_relay"]!.GetValue<bool>(), o["via_ping"]!.GetValue<bool>(),
                o["rtt_ms"]?.GetValue<long>(), o["device_id"]?.GetValue<string>());
        }).ToArray();
    }

    /// <summary>Triggers an immediate <c>gateway.link.ping</c> ESP-NOW broadcast — <c>gateway.ping.now</c>.</summary>
    public Task PingNowAsync(TimeSpan? timeout = null, CancellationToken cancellationToken = default)
        => SendAsync(ServiceId.Gateway, "gateway.ping.now", null, controlSessionId: 0,
            timeout ?? TimeSpan.FromSeconds(5), cancellationToken);

    public ValueTask DisposeAsync() => _controlSession.DisposeAsync();
}
