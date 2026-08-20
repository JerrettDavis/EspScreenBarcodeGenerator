namespace EspBarcode.Generator.Tests;

public class PayloadSourceTests
{
    [Fact]
    public void Resolve_LiteralArgument_ReturnedUnchanged()
    {
        Assert.Equal("LAB-TEST-001", PayloadSource.Resolve("LAB-TEST-001"));
    }

    [Fact]
    public void Resolve_AtPrefixedArgument_ReadsFileContents()
    {
        var path = Path.GetTempFileName();
        try
        {
            File.WriteAllText(path, "FROM-FILE-PAYLOAD\n");
            Assert.Equal("FROM-FILE-PAYLOAD", PayloadSource.Resolve("@" + path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Resolve_AtPrefixedMissingFile_ThrowsPayloadFileNotFound()
    {
        var ex = Assert.Throws<BarcodeGenerationException>(() => PayloadSource.Resolve("@" + Path.Combine(Path.GetTempPath(), "does-not-exist-" + Guid.NewGuid() + ".txt")));
        Assert.Equal("payload_file_not_found", ex.Code);
    }
}
