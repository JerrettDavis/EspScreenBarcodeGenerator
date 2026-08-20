using EspBarcode.Generator.Encoding;
using ZXing.Aztec;
using ZXing.Datamatrix;
using ZXing.PDF417;
using ZXing.QrCode;

namespace EspBarcode.Generator.Tests;

public class MatrixEncodersTests
{
    [Fact]
    public void EncodeQr_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Qr, Data = "LAB-TEST-001" };
        var matrix = MatrixEncoders.EncodeQr(spec);
        var reader = new QRCodeReader();
        var result = reader.decode(TestDecodeHelpers.ToBinaryBitmap(matrix));
        Assert.Equal("LAB-TEST-001", result!.Text);
    }

    [Fact]
    public void EncodeDataMatrix_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.DataMatrix, Data = "DM-ROUNDTRIP-123" };
        var matrix = MatrixEncoders.EncodeDataMatrix(spec);
        var reader = new DataMatrixReader();
        var result = reader.decode(TestDecodeHelpers.ToBinaryBitmap(matrix));
        Assert.Equal("DM-ROUNDTRIP-123", result!.Text);
    }

    [Fact]
    public void EncodeDataMatrix_Rectangular_ProducesWiderThanTallMatrix()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.DataMatrix, Data = "12345678", Rectangular = true };
        var matrix = MatrixEncoders.EncodeDataMatrix(spec);
        Assert.NotEqual(matrix.Width, matrix.Height);
    }

    [Fact]
    public void EncodeAztec_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Aztec, Data = "AZTEC-TEST" };
        var matrix = MatrixEncoders.EncodeAztec(spec);
        var reader = new AztecReader();
        var result = reader.decode(TestDecodeHelpers.ToBinaryBitmap(matrix));
        Assert.Equal("AZTEC-TEST", result!.Text);
    }

    [Fact]
    public void EncodePdf417_RoundTrips()
    {
        var spec = new BarcodeSpec { Type = BarcodeType.Pdf417, Data = "PDF417-TEST" };
        var matrix = MatrixEncoders.EncodePdf417(spec);
        var reader = new PDF417Reader();
        var result = reader.decode(TestDecodeHelpers.ToBinaryBitmap(matrix));
        Assert.Equal("PDF417-TEST", result!.Text);
    }
}
