using System.Text.Json;
using System.Text.Json.Serialization;

namespace EspBarcode.Generator;

/// <summary>
/// Serializes <see cref="BarcodeType"/> as the same wire-value string the CLI, the firmware and
/// <c>tools/espbarcode.py</c> use (<c>"qr"</c>, <c>"datamatrix"</c>, <c>"gs1-128"</c>, ...), by
/// delegating to <see cref="BarcodeTypeExtensions.ToWireValue"/>/<see cref="BarcodeTypeExtensions.ParseWireValue"/>
/// so those stay the single source of truth for the vocabulary.
/// </summary>
/// <remarks>
/// System.Text.Json's default enum handling would otherwise put the raw ordinal on the wire, which
/// made <c>POST /render {"type":"qr"}</c> — the documented spelling, and the one a third-party test
/// orchestrator would send — fail with 400 while <c>{"type":0}</c> succeeded. The built-in
/// <c>JsonStringEnumConverter</c> is not a substitute either: it spells members by their C# names
/// (<c>"DataMatrix"</c>, <c>"Gs1_128"</c>), not the protocol's.
/// <para>
/// Applied via <c>[JsonConverter]</c> on the enum itself rather than registered into a
/// <see cref="JsonSerializerOptions"/> instance, so both ends of the <c>/render</c> contract — the
/// GUI's minimal-API model binding and the CLI's <c>PostAsJsonAsync</c>, which each build their own
/// options — agree without either having to opt in.
/// </para>
/// </remarks>
public sealed class BarcodeTypeJsonConverter : JsonConverter<BarcodeType>
{
    public override BarcodeType Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType != JsonTokenType.String)
        {
            throw new JsonException(
                $"Expected a barcode type wire value such as \"qr\", got a JSON {reader.TokenType} token.");
        }

        var value = reader.GetString();
        if (value is null) throw new JsonException("Barcode type must not be null.");

        try
        {
            return BarcodeTypeExtensions.ParseWireValue(value);
        }
        catch (BarcodeGenerationException ex)
        {
            // JsonException is what the deserializer contract expects; carrying the message through
            // keeps the unknown-type reason visible in the 400 the viewer returns.
            throw new JsonException(ex.Message, ex);
        }
    }

    public override void Write(Utf8JsonWriter writer, BarcodeType value, JsonSerializerOptions options)
        => writer.WriteStringValue(value.ToWireValue());
}
