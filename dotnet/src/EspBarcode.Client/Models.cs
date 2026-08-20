namespace EspBarcode.Client;

/// <summary>Fields accepted by the <c>generate</c> command (docs/PROTOCOL.md).</summary>
public sealed record GenerateOptions
{
    public required BarcodeType Type { get; init; }
    public required string Data { get; init; }
    public bool Display { get; init; } = true;
    public string? SaveAs { get; init; }
    public string Ecc { get; init; } = "M";
    public string Rotation { get; init; } = "auto";
    public int Quiet { get; init; } = -1;
    public int MinModule { get; init; } = 2;
    public bool Rectangular { get; init; }
    public bool Invert { get; init; }
    public bool Checksum { get; init; } = true;
    public int QrMinVersion { get; init; } = 1;
    public int QrMaxVersion { get; init; } = 20;
    public int AztecSecurity { get; init; } = 23;
    public int AztecLayers { get; init; } = 1;
}

public sealed record GenerateResult(
    string Type,
    int Width,
    int Height,
    bool Linear,
    int Quiet,
    bool Displayed,
    string NormalizedData);

public sealed record DeviceInfo(
    string Device,
    string Firmware,
    string Protocol,
    string Transport,
    int ScreenWidth,
    int ScreenHeight);

public sealed record CapabilitiesInfo(
    IReadOnlyList<string> Symbologies,
    IReadOnlyList<string> Commands,
    int PayloadBytes,
    int SerialLineBytes,
    int MatrixWidth,
    int MatrixHeight,
    bool RawMatrix,
    bool StandaloneTouchUi,
    bool PersistentPresets);

public sealed record StatusInfo(
    bool BarcodeVisible,
    bool HasCurrent,
    bool CurrentIsRaw,
    string Status,
    long FreeHeap);

/// <summary>Fields accepted by <c>upload_begin</c> for a raw matrix transfer.</summary>
public sealed record RawMatrixOptions
{
    public bool Linear { get; init; }
    public int Quiet { get; init; } = 4;
    public string Rotation { get; init; } = "auto";
    public bool Invert { get; init; }
    public string Label { get; init; } = "";
    public bool Display { get; init; } = true;

    /// <summary>Decoded bytes per <c>upload_chunk</c>. Protocol range is 48-768; 384 is the documented recommendation.</summary>
    public int ChunkBytes { get; init; } = 384;
}

public sealed record UploadResult(int Width, int Height, bool Linear, uint Crc32, bool Displayed);
