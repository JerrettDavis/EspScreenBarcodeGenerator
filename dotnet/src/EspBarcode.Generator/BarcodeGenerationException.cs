namespace EspBarcode.Generator;

/// <summary>Thrown for any generation failure: invalid spec, unsupported combination, or symbol too dense for its target canvas.</summary>
public sealed class BarcodeGenerationException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
