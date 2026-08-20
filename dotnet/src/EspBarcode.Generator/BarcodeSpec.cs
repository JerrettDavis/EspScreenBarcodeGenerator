namespace EspBarcode.Generator;

/// <summary>Mirrors docs/PROTOCOL.md's generate command fields one-to-one, so the option vocabulary is identical across firmware, Python tool, EspBarcode.Client, and this generator.</summary>
public sealed record BarcodeSpec
{
    public required BarcodeType Type { get; init; }
    public required string Data { get; init; }
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
