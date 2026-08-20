#include <unity.h>

#include "EspBarcodeCore.h"

using namespace espbarcode;

void test_base64_round_trip() {
    const std::vector<uint8_t> bytes = {0, 1, 2, 253, 254, 255};
    std::vector<uint8_t> decoded;
    TEST_ASSERT_TRUE(bytesFromBase64(bytesToBase64(bytes), decoded));
    TEST_ASSERT_EQUAL_UINT32(bytes.size(), decoded.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), decoded.data(), bytes.size());
}

void test_retail_check_digits() {
    BarcodeSpec spec;
    spec.type = Symbology::Ean13;
    spec.data = "590123412345";
    auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_STRING("5901234123457", result.normalizedData.c_str());

    spec.type = Symbology::UpcA;
    spec.data = "03600029145";
    result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_STRING("036000291452", result.normalizedData.c_str());
}

void test_matrix_encoders() {
    BarcodeSpec spec;
    spec.type = Symbology::DataMatrix;
    spec.data = "PIO-DATAMATRIX";
    auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_UINT16(result.matrix.width(), result.matrix.height());

    spec.type = Symbology::Aztec;
    spec.data = "PIO-AZTEC";
    result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_UINT16(result.matrix.width(), result.matrix.height());
}

void test_qr_dependency_adapter() {
    BarcodeSpec spec;
    spec.type = Symbology::QrCode;
    spec.data = "PIO-QR-ENCODER";
    const auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_TRUE(result.matrix.width() >= 21);
    TEST_ASSERT_EQUAL_UINT16(result.matrix.width(), result.matrix.height());
    TEST_ASSERT_TRUE(result.matrix.get(0, 0));
    TEST_ASSERT_TRUE(result.matrix.get(6, 6));
}

void test_gs1_normalized_data_is_json_safe() {
    BarcodeSpec spec;
    spec.type = Symbology::Gs1_128;
    spec.data = "0109501101530003{FNC1}10ABC";
    const auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    // ArduinoJson does not escape raw control bytes (e.g. the GS1 group
    // separator, 0x1D) when serializing, so normalizedData must never carry
    // one through into a field that gets echoed back in a JSON response.
    TEST_ASSERT_EQUAL_STRING("0109501101530003<GS>10ABC", result.normalizedData.c_str());
}

void test_pixel_exact_layout() {
    BitMatrix matrix(29, 29);
    const auto layout = calculateLayout(matrix, false, 320, 480, Rotation::Auto, 4, 2);
    TEST_ASSERT_TRUE_MESSAGE(layout.ok, layout.error.c_str());
    TEST_ASSERT_EQUAL_UINT16(8, layout.modulePixels);
    TEST_ASSERT_EQUAL_UINT16(0, static_cast<uint16_t>(layout.rotation));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_base64_round_trip);
    RUN_TEST(test_retail_check_digits);
    RUN_TEST(test_matrix_encoders);
    RUN_TEST(test_qr_dependency_adapter);
    RUN_TEST(test_gs1_normalized_data_is_json_safe);
    RUN_TEST(test_pixel_exact_layout);
    return UNITY_END();
}
