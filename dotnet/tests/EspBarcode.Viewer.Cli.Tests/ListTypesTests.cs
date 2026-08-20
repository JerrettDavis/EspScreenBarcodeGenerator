using EspBarcode.Viewer.Cli;

namespace EspBarcode.Viewer.Cli.Tests;

public class ListTypesTests
{
    [Fact]
    public void ListTypeWireValues_ReturnsAllFourteenTypes()
    {
        var values = CliApp.ListTypeWireValues();
        Assert.Equal(14, values.Count);
        Assert.Contains("qr", values);
        Assert.Contains("pdf417", values);
    }
}
