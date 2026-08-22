using System.Text.Json;
using System.Text.Json.Nodes;
using EspBarcode.Controller.Web.Models;
using EspBarcode.Protocol;

namespace EspBarcode.Controller.Web.Services;

public sealed class EspBarcodeProtocolException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}

/// <summary>
/// Async NDJSON v1 client (docs/PROTOCOL.md) over a <see cref="WebSerialConnection"/> — the in-browser
/// counterpart to <c>EspBarcode.Client.EspBarcodeClient</c>, which speaks the same wire protocol over a
/// desktop <see cref="System.IO.Ports.SerialPort"/>. Not thread-safe beyond its own internal request
/// gate: callers may issue concurrent calls, they just serialize onto the wire one at a time.
/// </summary>
public sealed class EspDeviceClient(WebSerialConnection connection)
{
    private static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(5);

    private readonly SemaphoreSlim _gate = new(1, 1);
    private long _nextId;

    public async Task<JsonObject> RequestAsync(
        string command, IReadOnlyDictionary<string, object?>? fields = null,
        TimeSpan? timeout = null, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            var id = Interlocked.Increment(ref _nextId);
            var payload = new JsonObject { ["id"] = id, ["cmd"] = command };
            if (fields is not null)
            {
                foreach (var (key, value) in fields) payload[key] = ToJsonNode(value);
            }

            await connection.WriteLineAsync(payload.ToJsonString(), cancellationToken);
            return await ReadMatchingResponseAsync(id, command, timeout ?? DefaultTimeout, cancellationToken);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async Task<JsonObject> ReadMatchingResponseAsync(
        long id, string command, TimeSpan timeout, CancellationToken cancellationToken)
    {
        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutCts.CancelAfter(timeout);

        try
        {
            while (true)
            {
                var raw = await connection.ReadLineAsync(timeoutCts.Token);
                if (string.IsNullOrEmpty(raw)) continue;

                JsonNode? node;
                try { node = JsonNode.Parse(raw); }
                catch (JsonException) { continue; } // boot-log chatter sharing the UART

                if (node is not JsonObject obj) continue;
                if (!obj.TryGetPropertyValue("id", out var respId) || respId is null) continue;
                if (respId.GetValue<long>() != id) continue;

                ThrowIfError(obj);
                return obj;
            }
        }
        catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException($"Timed out waiting for a '{command}' response from the device.");
        }
    }

    private static void ThrowIfError(JsonObject response)
    {
        if (!response.TryGetPropertyValue("ok", out var okNode) || okNode is null) return;
        if (okNode.GetValue<bool>()) return;

        var error = response["error"] as JsonObject;
        var code = error?["code"]?.GetValue<string>() ?? "device_error";
        var message = error?["message"]?.GetValue<string>() ?? response.ToJsonString();
        throw new EspBarcodeProtocolException(code, message);
    }

    private static JsonNode? ToJsonNode(object? value) => value switch
    {
        null => null,
        JsonNode node => node,
        string s => JsonValue.Create(s),
        bool b => JsonValue.Create(b),
        int i => JsonValue.Create(i),
        long l => JsonValue.Create(l),
        double d => JsonValue.Create(d),
        _ => JsonSerializer.SerializeToNode(value),
    };

    // ---- convenience commands (docs/PROTOCOL.md "Commands") ----

    public async Task<DeviceInfo> HelloAsync(CancellationToken ct = default)
    {
        var r = await RequestAsync("hello", cancellationToken: ct);
        var screen = r["screen"]!.AsObject();
        return new DeviceInfo(
            r["device"]!.GetValue<string>(), r["firmware"]!.GetValue<string>(),
            r["protocol"]!.GetValue<string>(), r["transport"]!.GetValue<string>(),
            screen["width"]!.GetValue<int>(), screen["height"]!.GetValue<int>());
    }

    public async Task<StatusInfo> StatusAsync(CancellationToken ct = default)
    {
        var r = await RequestAsync("status", cancellationToken: ct);
        GatewayLinkStatus? gatewayLink = null;
        if (r["gateway_link"] as JsonObject is { } link)
        {
            gatewayLink = new GatewayLinkStatus(
                link["connected"]!.GetValue<bool>(), link["age_ms"]!.GetValue<long>(),
                link["rtt_ms"]!.GetValue<long>(), link["gateway_id"]?.GetValue<string>() ?? "");
        }
        return new StatusInfo(
            r["barcode_visible"]!.GetValue<bool>(), r["has_current"]!.GetValue<bool>(),
            r["current_raw"]!.GetValue<bool>(), r["status"]!.GetValue<string>(), r["free_heap"]!.GetValue<long>(),
            gatewayLink);
    }

    public async Task<GenerateResult> GenerateAsync(GenerateOptions options, CancellationToken ct = default)
    {
        var fields = new Dictionary<string, object?>
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
        if (options.SaveAs is not null) fields["save_as"] = options.SaveAs;

        var r = await RequestAsync("generate", fields, cancellationToken: ct);
        return new GenerateResult(
            r["type"]!.GetValue<string>(), r["width"]!.GetValue<int>(), r["height"]!.GetValue<int>(),
            r["linear"]!.GetValue<bool>(), r["quiet"]!.GetValue<int>(), r["displayed"]!.GetValue<bool>(),
            r["normalized_data"]!.GetValue<string>());
    }

    public Task DisplayAsync(string? presetName = null, CancellationToken ct = default)
        => RequestAsync("display", presetName is null ? null : new Dictionary<string, object?> { ["name"] = presetName }, cancellationToken: ct);

    public Task CloseAsync(CancellationToken ct = default) => RequestAsync("close", cancellationToken: ct);

    public Task HomeAsync(CancellationToken ct = default) => RequestAsync("home", cancellationToken: ct);

    public Task SaveAsync(string name, CancellationToken ct = default)
        => RequestAsync("save", new Dictionary<string, object?> { ["name"] = name }, cancellationToken: ct);

    public Task LoadAsync(string name, bool display = false, CancellationToken ct = default)
        => RequestAsync("load", new Dictionary<string, object?> { ["name"] = name, ["display"] = display }, cancellationToken: ct);

    public Task DeleteAsync(string name, CancellationToken ct = default)
        => RequestAsync("delete", new Dictionary<string, object?> { ["name"] = name }, cancellationToken: ct);

    public async Task<IReadOnlyList<string>> ListPresetsAsync(CancellationToken ct = default)
        => (await RequestAsync("list", cancellationToken: ct))["presets"]!.AsArray().Select(n => n!.GetValue<string>()).ToArray();

    public Task BacklightAsync(bool on, CancellationToken ct = default)
        => RequestAsync("backlight", new Dictionary<string, object?> { ["on"] = on }, cancellationToken: ct);

    public Task SetOrientationAsync(string target, int value, CancellationToken ct = default)
        => RequestAsync("orientation", new Dictionary<string, object?> { ["target"] = target, ["value"] = value }, cancellationToken: ct);

    public Task RebootAsync(CancellationToken ct = default) => RequestAsync("reboot", cancellationToken: ct);

    /// <summary>Sends the v1 <c>upgrade</c> request that switches the firmware to EspLink v2 COBS framing.</summary>
    public Task UpgradeToV2Async(CancellationToken ct = default) => RequestAsync("upgrade", cancellationToken: ct);

    /// <summary>Sends the v1 <c>gateway</c> request that switches this board into a USB&lt;-&gt;ESP-NOW relay.</summary>
    public Task EnterGatewayModeAsync(CancellationToken ct = default) => RequestAsync("gateway", cancellationToken: ct);

    /// <summary>
    /// Downloads the currently displayed matrix via <c>download</c>/<c>download_chunk</c>/<c>download_end</c>,
    /// validating the final CRC32 — used to render an exact preview of what the device is showing.
    /// </summary>
    public async Task<DownloadedMatrix> DownloadCurrentMatrixAsync(int chunkBytes = 384, TimeSpan? timeout = null, CancellationToken ct = default)
    {
        await _gate.WaitAsync(ct);
        try
        {
            var id = Interlocked.Increment(ref _nextId);
            await connection.WriteLineAsync(
                new JsonObject { ["id"] = id, ["cmd"] = "download", ["chunk_bytes"] = chunkBytes }.ToJsonString(), ct);

            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeoutCts.CancelAfter(timeout ?? DefaultTimeout);

            int width = 0, height = 0;
            bool linear = false, invert = false;
            var label = "";
            uint expectedCrc = 0;
            byte[]? packed = null;

            try
            {
                while (true)
                {
                    var raw = await connection.ReadLineAsync(timeoutCts.Token);
                    if (string.IsNullOrEmpty(raw)) continue;

                    JsonNode? node;
                    try { node = JsonNode.Parse(raw); } catch (JsonException) { continue; }
                    if (node is not JsonObject obj) continue;
                    if (!obj.TryGetPropertyValue("id", out var respId) || respId is null || respId.GetValue<long>() != id) continue;

                    ThrowIfError(obj);

                    switch (obj["event"]?.GetValue<string>())
                    {
                        case "download_begin":
                            width = obj["width"]!.GetValue<int>();
                            height = obj["height"]!.GetValue<int>();
                            linear = obj["linear"]!.GetValue<bool>();
                            invert = obj["invert"]?.GetValue<bool>() ?? false;
                            label = obj["label"]?.GetValue<string>() ?? "";
                            expectedCrc = obj["crc32"]!.GetValue<uint>();
                            packed = new byte[obj["bytes"]!.GetValue<int>()];
                            break;
                        case "download_chunk":
                            if (packed is null) throw new InvalidOperationException("received a chunk before download_begin");
                            var offset = obj["offset"]!.GetValue<int>();
                            var data = Convert.FromBase64String(obj["data"]!.GetValue<string>());
                            Array.Copy(data, 0, packed, offset, data.Length);
                            break;
                        case "download_end":
                            if (packed is null) throw new InvalidOperationException("download ended before it began");
                            if (Crc32.Compute(packed) != expectedCrc)
                                throw new InvalidOperationException("downloaded matrix failed CRC32 validation");
                            return new DownloadedMatrix(width, height, linear, invert, label, packed);
                    }
                }
            }
            catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested && !ct.IsCancellationRequested)
            {
                throw new TimeoutException("Timed out waiting for the matrix download to complete.");
            }
        }
        finally
        {
            _gate.Release();
        }
    }
}
