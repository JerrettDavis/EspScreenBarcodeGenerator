using System.Text.Json;

namespace EspBarcode.Generator.Tests;

public class BarcodeTypeTests
{
    [Theory]
    [InlineData(BarcodeType.Qr, "qr")]
    [InlineData(BarcodeType.DataMatrix, "datamatrix")]
    [InlineData(BarcodeType.Aztec, "aztec")]
    [InlineData(BarcodeType.Code128, "code128")]
    [InlineData(BarcodeType.Gs1_128, "gs1-128")]
    [InlineData(BarcodeType.Code39, "code39")]
    [InlineData(BarcodeType.UpcA, "upca")]
    [InlineData(BarcodeType.Ean13, "ean13")]
    [InlineData(BarcodeType.Ean8, "ean8")]
    [InlineData(BarcodeType.Itf, "itf")]
    [InlineData(BarcodeType.Itf14, "itf14")]
    [InlineData(BarcodeType.Codabar, "codabar")]
    [InlineData(BarcodeType.Msi, "msi")]
    [InlineData(BarcodeType.Pdf417, "pdf417")]
    public void ToWireValue_And_ParseWireValue_RoundTrip(BarcodeType type, string wire)
    {
        Assert.Equal(wire, type.ToWireValue());
        Assert.Equal(type, BarcodeTypeExtensions.ParseWireValue(wire));
    }

    [Fact]
    public void ParseWireValue_UnknownValue_ThrowsWithUnknownTypeCode()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => BarcodeTypeExtensions.ParseWireValue("not-a-type"));
        Assert.Equal("unknown_type", ex.Code);
    }

    // ---- JSON wire form (the POST /render contract) --------------------------------------------

    private static readonly JsonSerializerOptions WebOptions = new(JsonSerializerDefaults.Web);

    [Theory]
    [InlineData(BarcodeType.Qr, "qr")]
    [InlineData(BarcodeType.DataMatrix, "datamatrix")]
    [InlineData(BarcodeType.Gs1_128, "gs1-128")]
    [InlineData(BarcodeType.Pdf417, "pdf417")]
    public void Serialize_UsesTheWireValueString_NotTheEnumOrdinalOrTheCSharpMemberName(BarcodeType type, string wire)
    {
        Assert.Equal($"\"{wire}\"", JsonSerializer.Serialize(type, WebOptions));
        Assert.Equal(type, JsonSerializer.Deserialize<BarcodeType>($"\"{wire}\"", WebOptions));
    }

    [Fact]
    public void DeserializeSpec_DocumentedWireVocabulary_IsAccepted()
    {
        // The shape a third-party test orchestrator posts to the viewer's /render endpoint.
        var spec = JsonSerializer.Deserialize<BarcodeSpec>(
            """{"type":"qr","data":"TEST","rotation":"90","minModule":3}""", WebOptions);

        Assert.NotNull(spec);
        Assert.Equal(BarcodeType.Qr, spec!.Type);
        Assert.Equal("TEST", spec.Data);
        Assert.Equal("90", spec.Rotation);
        Assert.Equal(3, spec.MinModule);
    }

    [Fact]
    public void SerializeSpec_RoundTripsThroughTheWireVocabulary()
    {
        var original = new BarcodeSpec { Type = BarcodeType.DataMatrix, Data = "DM-TEST", Rectangular = true };
        var json = JsonSerializer.Serialize(original, WebOptions);

        Assert.Contains("\"datamatrix\"", json);
        Assert.Equal(original, JsonSerializer.Deserialize<BarcodeSpec>(json, WebOptions));
    }

    [Fact]
    public void DeserializeType_EnumOrdinal_IsRejected()
    {
        // The old behavior, kept out on purpose: an ordinal is not part of the documented vocabulary
        // and silently means something different if the enum ever gains a member.
        Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<BarcodeType>("0", WebOptions));
    }

    [Fact]
    public void DeserializeType_UnknownWireValue_ReportsTheReason()
    {
        var ex = Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<BarcodeType>("\"not-a-type\"", WebOptions));
        Assert.Contains("not-a-type", ex.Message);
    }
}
