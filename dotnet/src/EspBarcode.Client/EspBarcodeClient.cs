using System.Text.Json;
using System.Text.Json.Nodes;
using EspBarcode.Client.Transport;
using EspBarcode.Generator;

namespace EspBarcode.Client;

/// <summary>
/// Client for the EspScreenBarcodeGenerator USB-serial NDJSON protocol
/// (protocol 1.0 — see docs/PROTOCOL.md). Not thread-safe: issue one request
/// at a time.
/// </summary>
public sealed class EspBarcodeClient : IDisposable
{
    private readonly IEspBarcodeTransport _transport;
    private readonly TimeSpan _timeout;
    private long _nextId;

    public EspBarcodeClient(IEspBarcodeTransport transport, TimeSpan? timeout = null)
    {
        _transport = transport;
        _timeout = timeout ?? TimeSpan.FromSeconds(3);
    }

    /// <summary>Opens a real serial connection to the device.</summary>
    public static EspBarcodeClient Connect(string portName, int baudRate = 115200, TimeSpan? timeout = null)
        => new(new SerialPortTransport(portName, baudRate), timeout);

    /// <summary>
    /// Sends a request and returns the matching response object, retrying past
    /// any unrelated lines (boot-log chatter, the unsolicited <c>ready</c>
    /// event) until a response carrying the same <c>id</c> arrives or the
    /// request times out. Throws <see cref="EspBarcodeProtocolException"/> on
    /// <c>"ok":false</c>.
    /// </summary>
    public JsonObject Request(string command, IReadOnlyDictionary<string, object?>? fields = null)
    {
        var id = Interlocked.Increment(ref _nextId);
        var payload = new JsonObject { ["id"] = id, ["cmd"] = command };
        if (fields is not null)
        {
            foreach (var (key, value) in fields)
            {
                payload[key] = ToJsonNode(value);
            }
        }

        _transport.WriteLine(payload.ToJsonString());
        return ReadMatchingResponse(id, command);
    }

    private JsonObject ReadMatchingResponse(long id, string command)
    {
        var deadline = DateTime.UtcNow + _timeout;
        while (DateTime.UtcNow < deadline)
        {
            var raw = _transport.ReadLine();
            if (string.IsNullOrEmpty(raw)) continue;

            JsonNode? node;
            try
            {
                node = JsonNode.Parse(raw);
            }
            catch (JsonException)
            {
                // Non-protocol chatter (e.g. boot-time log lines emitted after a
                // DTR-triggered reset) shares the UART; skip it and keep listening.
                continue;
            }

            if (node is not JsonObject obj) continue;
            if (!obj.TryGetPropertyValue("id", out var respId) || respId is null) continue;
            if (respId.GetValue<long>() != id) continue;

            ThrowIfError(obj);
            return obj;
        }

        throw new TimeoutException($"Timed out waiting for a '{command}' response from the device.");
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
        uint u => JsonValue.Create(u),
        double d => JsonValue.Create(d),
        _ => JsonSerializer.SerializeToNode(value),
    };

    // ---- convenience commands (docs/PROTOCOL.md "Commands") ----

    public DeviceInfo Hello()
    {
        var r = Request("hello");
        var screen = r["screen"]!.AsObject();
        return new DeviceInfo(
            r["device"]!.GetValue<string>(),
            r["firmware"]!.GetValue<string>(),
            r["protocol"]!.GetValue<string>(),
            r["transport"]!.GetValue<string>(),
            screen["width"]!.GetValue<int>(),
            screen["height"]!.GetValue<int>());
    }

    public CapabilitiesInfo Capabilities()
    {
        var r = Request("capabilities");
        var limits = r["limits"]!.AsObject();
        return new CapabilitiesInfo(
            r["symbologies"]!.AsArray().Select(n => n!.GetValue<string>()).ToArray(),
            r["commands"]!.AsArray().Select(n => n!.GetValue<string>()).ToArray(),
            limits["payload_bytes"]!.GetValue<int>(),
            limits["serial_line_bytes"]!.GetValue<int>(),
            limits["matrix_width"]!.GetValue<int>(),
            limits["matrix_height"]!.GetValue<int>(),
            r["raw_matrix"]!.GetValue<bool>(),
            r["standalone_touch_ui"]!.GetValue<bool>(),
            r["persistent_presets"]!.GetValue<bool>());
    }

    public StatusInfo Status()
    {
        var r = Request("status");
        return new StatusInfo(
            r["barcode_visible"]!.GetValue<bool>(),
            r["has_current"]!.GetValue<bool>(),
            r["current_raw"]!.GetValue<bool>(),
            r["status"]!.GetValue<string>(),
            r["free_heap"]!.GetValue<long>());
    }

    public GenerateResult Generate(GenerateOptions options)
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

        var r = Request("generate", fields);
        return new GenerateResult(
            r["type"]!.GetValue<string>(),
            r["width"]!.GetValue<int>(),
            r["height"]!.GetValue<int>(),
            r["linear"]!.GetValue<bool>(),
            r["quiet"]!.GetValue<int>(),
            r["displayed"]!.GetValue<bool>(),
            r["normalized_data"]!.GetValue<string>());
    }

    public void Display(string? presetName = null)
        => Request("display", presetName is null ? null : new Dictionary<string, object?> { ["name"] = presetName });

    public void Close() => Request("close");

    public void Home() => Request("home");

    public void Save(string name) => Request("save", new Dictionary<string, object?> { ["name"] = name });

    public void Load(string name, bool display = false)
        => Request("load", new Dictionary<string, object?> { ["name"] = name, ["display"] = display });

    public void Delete(string name) => Request("delete", new Dictionary<string, object?> { ["name"] = name });

    public IReadOnlyList<string> ListPresets()
        => Request("list")["presets"]!.AsArray().Select(n => n!.GetValue<string>()).ToArray();

    public void Backlight(bool on) => Request("backlight", new Dictionary<string, object?> { ["on"] = on });

    public void Reboot() => Request("reboot");

    /// <summary>
    /// Uploads a host-rendered module matrix (e.g. a PDF417 symbol the device
    /// can't generate on-board) via <c>upload_begin</c>/<c>upload_chunk</c>/
    /// <c>upload_end</c>, CRC32-validated end to end.
    /// </summary>
    public UploadResult UploadRawMatrix(RawMatrix matrix, RawMatrixOptions options)
    {
        var packed = matrix.Pack();

        Request("upload_begin", new Dictionary<string, object?>
        {
            ["width"] = matrix.Width,
            ["height"] = matrix.Height,
            ["linear"] = options.Linear,
            ["quiet"] = options.Quiet,
            ["rotation"] = options.Rotation,
            ["invert"] = options.Invert,
            ["label"] = options.Label,
            ["display"] = options.Display,
        });

        var chunkSize = Math.Clamp(options.ChunkBytes, 48, 768);
        for (var offset = 0; offset < packed.Length; offset += chunkSize)
        {
            var length = Math.Min(chunkSize, packed.Length - offset);
            var chunk = new byte[length];
            Array.Copy(packed, offset, chunk, 0, length);
            Request("upload_chunk", new Dictionary<string, object?>
            {
                ["offset"] = offset,
                ["data"] = Convert.ToBase64String(chunk),
            });
        }

        var crc = Crc32.Compute(packed);
        var end = Request("upload_end", new Dictionary<string, object?> { ["crc32"] = crc });
        return new UploadResult(
            matrix.Width,
            matrix.Height,
            options.Linear,
            crc,
            end["displayed"]?.GetValue<bool>() ?? options.Display);
    }

    public void UploadAbort() => Request("upload_abort");

    /// <summary>
    /// Downloads the currently displayed matrix via the multi-response
    /// <c>download</c>/<c>download_chunk</c>/<c>download_end</c> sequence,
    /// validating the final CRC32 before returning.
    /// </summary>
    public (RawMatrix Matrix, RawMatrixOptions Options) DownloadRawMatrix(int chunkBytes = 384)
    {
        var id = Interlocked.Increment(ref _nextId);
        _transport.WriteLine(new JsonObject
        {
            ["id"] = id,
            ["cmd"] = "download",
            ["chunk_bytes"] = chunkBytes,
        }.ToJsonString());

        int width = 0, height = 0;
        var linear = false;
        var invert = false;
        var rotation = "auto";
        var label = "";
        uint expectedCrc = 0;
        byte[]? packedBuffer = null;

        var deadline = DateTime.UtcNow + _timeout;
        while (DateTime.UtcNow < deadline)
        {
            var raw = _transport.ReadLine();
            if (string.IsNullOrEmpty(raw)) continue;

            JsonNode? node;
            try
            {
                node = JsonNode.Parse(raw);
            }
            catch (JsonException)
            {
                continue;
            }

            if (node is not JsonObject obj) continue;
            if (!obj.TryGetPropertyValue("id", out var respId) || respId is null || respId.GetValue<long>() != id) continue;

            ThrowIfError(obj);
            deadline = DateTime.UtcNow + _timeout;

            switch (obj["event"]?.GetValue<string>())
            {
                case "download_begin":
                    width = obj["width"]!.GetValue<int>();
                    height = obj["height"]!.GetValue<int>();
                    linear = obj["linear"]!.GetValue<bool>();
                    rotation = obj["rotation"]?.GetValue<string>() ?? "auto";
                    invert = obj["invert"]?.GetValue<bool>() ?? false;
                    label = obj["label"]?.GetValue<string>() ?? "";
                    expectedCrc = obj["crc32"]!.GetValue<uint>();
                    packedBuffer = new byte[obj["bytes"]!.GetValue<int>()];
                    break;

                case "download_chunk":
                    if (packedBuffer is null) throw new InvalidOperationException("received a chunk before download_begin");
                    var offset = obj["offset"]!.GetValue<int>();
                    var data = Convert.FromBase64String(obj["data"]!.GetValue<string>());
                    Array.Copy(data, 0, packedBuffer, offset, data.Length);
                    break;

                case "download_end":
                    if (packedBuffer is null) throw new InvalidOperationException("download ended before it began");
                    if (Crc32.Compute(packedBuffer) != expectedCrc)
                        throw new InvalidOperationException("downloaded matrix failed CRC32 validation");
                    var matrix = RawMatrix.Unpack(width, height, packedBuffer);
                    var options = new RawMatrixOptions { Linear = linear, Rotation = rotation, Invert = invert, Label = label };
                    return (matrix, options);
            }
        }

        throw new TimeoutException("Timed out waiting for the matrix download to complete.");
    }

    public void Dispose() => _transport.Dispose();
}
