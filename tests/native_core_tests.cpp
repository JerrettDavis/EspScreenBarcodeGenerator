#include "EspBarcodeCore.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace espbarcode;

namespace {
int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
}

#define CHECK(expr) check((expr), #expr, __LINE__)

BarcodeResult make(Symbology type, const std::string& data) {
    BarcodeSpec spec;
    spec.type = type;
    spec.data = data;
    return encode(spec);
}

void testMatrixPacking() {
    BitMatrix matrix(9, 2);
    CHECK(matrix.width() == 9);
    CHECK(matrix.height() == 2);
    CHECK(matrix.packed().size() == 3);
    matrix.set(0, 0);
    matrix.set(8, 0);
    matrix.set(4, 1);
    CHECK(matrix.get(0, 0));
    CHECK(matrix.get(8, 0));
    CHECK(matrix.get(4, 1));
    CHECK(!matrix.get(1, 0));
    CHECK(matrix.packed()[0] == 0x80);
    CHECK(matrix.packed()[1] == 0x84);
    CHECK(matrix.packed()[2] == 0x00);
    matrix.clear();
    CHECK(!matrix.get(0, 0));
    CHECK(!matrix.resize(0, 1));
    CHECK(!matrix.resize(513, 512));
}

void testBase64() {
    const std::vector<uint8_t> input = {0x00, 0x01, 0x02, 0xFE, 0xFF};
    const std::string encoded = bytesToBase64(input);
    CHECK(encoded == "AAEC/v8=");
    std::vector<uint8_t> decoded;
    CHECK(bytesFromBase64(encoded, decoded));
    CHECK(decoded == input);
    CHECK(!bytesFromBase64("A", decoded));
    CHECK(!bytesFromBase64("AA=A", decoded));
    CHECK(!bytesFromBase64("!!!!", decoded));

    BitMatrix source(11, 7);
    source.set(0, 0);
    source.set(10, 6);
    BitMatrix restored;
    CHECK(packedFromBase64(11, 7, packedToBase64(source), restored));
    CHECK(restored.packed() == source.packed());
}

void testParsing() {
    Symbology symbology;
    CHECK(tryParseSymbology("GS1-128", symbology) && symbology == Symbology::Gs1_128);
    CHECK(tryParseSymbology("Data Matrix", symbology) && symbology == Symbology::DataMatrix);
    CHECK(!tryParseSymbology("pdf417", symbology));

    Rotation rotation;
    CHECK(tryParseRotation("270", rotation) && rotation == Rotation::Deg270);
    CHECK(tryParseRotation("auto", rotation) && rotation == Rotation::Auto);
    CHECK(!tryParseRotation("45", rotation));

    ErrorCorrection ecc;
    CHECK(tryParseErrorCorrection("quartile", ecc) && ecc == ErrorCorrection::Quartile);
}

void testLinearEncoders() {
    auto code128 = make(Symbology::Code128, "Hello-123");
    CHECK(code128.ok && code128.linear && code128.matrix.height() == 1);
    CHECK(code128.normalizedData == "Hello-123");

    auto gs1 = make(Symbology::Gs1_128, "0109501101530003{FNC1}10ABC");
    CHECK(gs1.ok);
    // normalizedData is echoed back in JSON responses, and ArduinoJson does not
    // escape raw control bytes, so the group separator must be reported as a
    // printable token rather than the raw 0x1D byte.
    CHECK(gs1.normalizedData.find(static_cast<char>(29)) == std::string::npos);
    CHECK(gs1.normalizedData == "0109501101530003<GS>10ABC");

    auto code39 = make(Symbology::Code39, "LAB-39");
    CHECK(code39.ok && code39.normalizedData == "LAB-39");
    CHECK(make(Symbology::Code39, "lowercase").normalizedData == "LOWERCASE");

    auto ean13 = make(Symbology::Ean13, "590123412345");
    CHECK(ean13.ok && ean13.normalizedData == "5901234123457");
    CHECK(make(Symbology::Ean13, "5901234123457").ok);
    CHECK(!make(Symbology::Ean13, "5901234123458").ok);

    auto ean8 = make(Symbology::Ean8, "5512345");
    CHECK(ean8.ok && ean8.normalizedData == "55123457");

    auto upca = make(Symbology::UpcA, "03600029145");
    CHECK(upca.ok && upca.normalizedData == "036000291452");

    auto itf = make(Symbology::Itf, "12345670");
    CHECK(itf.ok && itf.normalizedData == "12345670");
    CHECK(make(Symbology::Itf, "123").normalizedData == "0123");

    auto itf14 = make(Symbology::Itf14, "1001234500001");
    CHECK(itf14.ok && itf14.normalizedData == "10012345000017");

    auto codabar = make(Symbology::Codabar, "A12345B");
    CHECK(codabar.ok && codabar.normalizedData == "A12345B");
    CHECK(make(Symbology::Codabar, "12345").normalizedData == "A12345A");

    BarcodeSpec msiSpec;
    msiSpec.type = Symbology::Msi;
    msiSpec.data = "12345";
    msiSpec.checksum = true;
    const auto msi = encode(msiSpec);
    CHECK(msi.ok && msi.normalizedData == "123455");
}

void testMatrixEncoders() {
    BarcodeSpec dmSpec;
    dmSpec.type = Symbology::DataMatrix;
    dmSpec.data = "DM-ROUNDTRIP-123";
    auto dm = encode(dmSpec);
    CHECK(dm.ok && !dm.linear && dm.matrix.width() == 18 && dm.matrix.height() == 18);
    CHECK(dm.defaultQuietZone == 1);
    CHECK(dm.matrix.get(0, 0));
    CHECK(dm.matrix.get(0, dm.matrix.height() - 1));

    dmSpec.data = "RECT-123";
    dmSpec.dataMatrixRectangular = true;
    dm = encode(dmSpec);
    CHECK(dm.ok && dm.matrix.width() != dm.matrix.height());

    BarcodeSpec aztecSpec;
    aztecSpec.type = Symbology::Aztec;
    aztecSpec.data = "AZTEC-ROUNDTRIP-123";
    auto aztec = encode(aztecSpec);
    CHECK(aztec.ok && aztec.matrix.width() == aztec.matrix.height());
    CHECK(aztec.defaultQuietZone == 2);

    aztecSpec.data = "42";
    aztecSpec.aztecMinLayers = 0;
    auto rune = encode(aztecSpec);
    CHECK(rune.ok && rune.matrix.width() == 13 && rune.matrix.height() == 13);

#if __has_include(<qrcode.h>)
    BarcodeSpec qrSpec;
    qrSpec.type = Symbology::QrCode;
    qrSpec.data = "QR-ADAPTER-TEST";
    auto qr = encode(qrSpec);
    CHECK(qr.ok && qr.matrix.width() >= 21 && qr.matrix.width() == qr.matrix.height());
    CHECK(qr.matrix.get(0, 0));
    CHECK(qr.matrix.get(6, 6));
#endif
}

void testLayout() {
    BitMatrix matrix(29, 29);
    auto layout = calculateLayout(matrix, false, 320, 480, Rotation::Auto, 4, 2);
    CHECK(layout.ok);
    CHECK(layout.modulePixels == 8);
    CHECK(layout.rotation == Rotation::Deg0);
    CHECK(layout.widthPixels == (29 + 8) * 8);
    CHECK(layout.x == 12);

    BitMatrix linear(120, 1);
    layout = calculateLayout(linear, true, 320, 480, Rotation::Auto, 10, 2);
    CHECK(layout.ok);
    CHECK(layout.rotation == Rotation::Deg90);
    CHECK(layout.modulePixels >= 2);

    BitMatrix dense(200, 200);
    layout = calculateLayout(dense, false, 320, 480, Rotation::Deg0, 4, 2);
    CHECK(!layout.ok);
}

void testLimits() {
    BarcodeSpec spec;
    spec.type = Symbology::DataMatrix;
    spec.data.assign(2049, 'A');
    const auto result = encode(spec);
    CHECK(!result.ok);
    CHECK(result.error.find("2048") != std::string::npos);
}
}

int main() {
    testMatrixPacking();
    testBase64();
    testParsing();
    testLinearEncoders();
    testMatrixEncoders();
    testLayout();
    testLimits();
    if (failures != 0) {
        std::cerr << failures << " native core test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All native core tests passed\n";
    return EXIT_SUCCESS;
}
