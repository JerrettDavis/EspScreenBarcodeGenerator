using System.Text.Json;
using EspBarcode.Generator;

namespace EspBarcode.Client.Tests;

public class EspBarcodeClientTests
{
    [Fact]
    public void Hello_ParsesDeviceInfo()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"hello","device":"EspScreenBarcodeGenerator","firmware":"0.1.0","protocol":"1.0","transport":"usb-uart-ndjson","screen":{"width":320,"height":480}}""");
        using var client = new EspBarcodeClient(transport);

        var hello = client.Hello();

        Assert.Equal("EspScreenBarcodeGenerator", hello.Device);
        Assert.Equal("0.1.0", hello.Firmware);
        Assert.Equal(320, hello.ScreenWidth);
        Assert.Equal(480, hello.ScreenHeight);
    }

    [Fact]
    public void Request_SkipsBootLogChatterAndUnsolicitedReadyEvent()
    {
        var transport = new FakeTransport();
        // Boot-time log line (not JSON at all) followed by the unsolicited
        // "ready" event (valid JSON, but no "id" to correlate against).
        transport.Enqueue("E (219) esp_core_dump_flash: No dump partition found!");
        transport.Enqueue("""{"event":"ready","device":"EspScreenBarcodeGenerator","protocol":"1.0","firmware":"0.1.0"}""");
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"status","barcode_visible":false,"has_current":false,"current_raw":false,"status":"Ready","free_heap":344052}""");
        using var client = new EspBarcodeClient(transport);

        var status = client.Status();

        Assert.Equal("Ready", status.Status);
        Assert.False(status.HasCurrent);
    }

    [Fact]
    public void Request_SkipsResponsesForOtherRequestIds()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":999,"ok":true,"cmd":"status","barcode_visible":true,"has_current":true,"current_raw":false,"status":"stale","free_heap":1}""");
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"status","barcode_visible":false,"has_current":false,"current_raw":false,"status":"Ready","free_heap":344052}""");
        using var client = new EspBarcodeClient(transport);

        var status = client.Status();

        Assert.Equal("Ready", status.Status);
    }

    [Fact]
    public void Request_ThrowsProtocolExceptionOnError()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":false,"cmd":"generate","error":{"code":"generation_failed","message":"EAN-13 check digit is invalid"}}""");
        using var client = new EspBarcodeClient(transport);

        var ex = Assert.Throws<EspBarcodeProtocolException>(() => client.Generate(new GenerateOptions { Type = BarcodeType.Ean13, Data = "590123412340" }));

        Assert.Equal("generation_failed", ex.Code);
        Assert.Equal("EAN-13 check digit is invalid", ex.Message);
    }

    [Fact]
    public void Request_TimesOutWhenNoMatchingResponseArrives()
    {
        var transport = new FakeTransport();
        using var client = new EspBarcodeClient(transport, TimeSpan.FromMilliseconds(50));

        Assert.Throws<TimeoutException>(() => client.Status());
    }

    [Fact]
    public void Generate_SendsDocumentedTopLevelFields()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"generate","type":"qr","width":21,"height":21,"linear":false,"quiet":4,"displayed":true,"normalized_data":"LAB-TEST-001"}""");
        using var client = new EspBarcodeClient(transport);

        var result = client.Generate(new GenerateOptions { Type = BarcodeType.Qr, Data = "LAB-TEST-001" });

        Assert.Equal("qr", result.Type);
        Assert.Equal(21, result.Width);
        Assert.Equal("LAB-TEST-001", result.NormalizedData);

        var sent = JsonDocument.Parse(transport.WrittenLines.Single());
        Assert.Equal("generate", sent.RootElement.GetProperty("cmd").GetString());
        Assert.Equal("qr", sent.RootElement.GetProperty("type").GetString());
        Assert.Equal("LAB-TEST-001", sent.RootElement.GetProperty("data").GetString());
        Assert.True(sent.RootElement.GetProperty("display").GetBoolean());
    }

    [Fact]
    public void Generate_Gs1128_SendsFnc1TokenVerbatim()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"generate","type":"gs1-128","width":200,"height":1,"linear":true,"quiet":10,"displayed":true,"normalized_data":"0109501101530003<GS>10ABC"}""");
        using var client = new EspBarcodeClient(transport);

        var result = client.Generate(new GenerateOptions
        {
            Type = BarcodeType.Gs1_128,
            Data = "0109501101530003{FNC1}10ABC",
        });

        Assert.Equal("0109501101530003<GS>10ABC", result.NormalizedData);
        var sent = JsonDocument.Parse(transport.WrittenLines.Single());
        Assert.Equal("gs1-128", sent.RootElement.GetProperty("type").GetString());
    }

    [Fact]
    public void UploadRawMatrix_ChunksAndSendsCrcMatchingPackedBytes()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"upload_begin","bytes_expected":2,"next_offset":0}""");
        transport.Enqueue("""{"id":2,"ok":true,"cmd":"upload_chunk","next_offset":2}""");
        transport.Enqueue("""{"id":3,"ok":true,"cmd":"upload_end","crc32":0,"displayed":true}""");
        using var client = new EspBarcodeClient(transport);

        var matrix = new RawMatrix(3, 2);
        matrix[0, 0] = true;
        matrix[2, 0] = true;
        matrix[1, 1] = true;

        var result = client.UploadRawMatrix(matrix, new RawMatrixOptions { Label = "external-pdf417" });

        Assert.True(result.Displayed);
        Assert.Equal(3, transport.WrittenLines.Count);

        var begin = JsonDocument.Parse(transport.WrittenLines[0]).RootElement;
        Assert.Equal("upload_begin", begin.GetProperty("cmd").GetString());
        Assert.Equal(3, begin.GetProperty("width").GetInt32());
        Assert.Equal(2, begin.GetProperty("height").GetInt32());

        var chunk = JsonDocument.Parse(transport.WrittenLines[1]).RootElement;
        Assert.Equal("upload_chunk", chunk.GetProperty("cmd").GetString());
        Assert.Equal(0, chunk.GetProperty("offset").GetInt32());
        Assert.Equal("qA==", chunk.GetProperty("data").GetString()); // docs/PROTOCOL.md's own 3x2 example packs to 0xA8

        var end = JsonDocument.Parse(transport.WrittenLines[2]).RootElement;
        Assert.Equal("upload_end", end.GetProperty("cmd").GetString());
        Assert.Equal((byte)0xA8, matrix.Pack()[0]);
    }

    [Fact]
    public void DownloadRawMatrix_AssemblesChunksAndValidatesCrc()
    {
        var transport = new FakeTransport();
        // 3x2 matrix "101/010" packs to the single byte 0xA8 (docs/PROTOCOL.md example).
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"download","event":"download_begin","width":3,"height":2,"linear":false,"quiet":4,"rotation":"auto","invert":false,"label":"QR Code","bytes":1,"encoding":"base64-packed-msb-first","crc32":168805463}""");
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"download","event":"download_chunk","offset":0,"data":"qA=="}""");
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"download","event":"download_end","bytes":1,"crc32":168805463}""");
        using var client = new EspBarcodeClient(transport);

        var (matrix, options) = client.DownloadRawMatrix();

        Assert.Equal(3, matrix.Width);
        Assert.Equal(2, matrix.Height);
        Assert.Equal("QR Code", options.Label);
        Assert.True(matrix[0, 0]);
        Assert.False(matrix[1, 0]);
        Assert.True(matrix[2, 0]);
        Assert.False(matrix[0, 1]);
        Assert.True(matrix[1, 1]);
        Assert.False(matrix[2, 1]);
    }

    [Fact]
    public void DownloadRawMatrix_ThrowsOnCrcMismatch()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"download","event":"download_begin","width":3,"height":2,"linear":false,"quiet":4,"rotation":"auto","invert":false,"label":"QR Code","bytes":1,"encoding":"base64-packed-msb-first","crc32":1}""");
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"download","event":"download_chunk","offset":0,"data":"qA=="}""");
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"download","event":"download_end","bytes":1,"crc32":1}""");
        using var client = new EspBarcodeClient(transport);

        Assert.Throws<InvalidOperationException>(() => client.DownloadRawMatrix());
    }
}
