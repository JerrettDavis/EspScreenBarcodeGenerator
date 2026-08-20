#include <unity.h>

#include <cstdio>
#include <random>

#include "EspBarcodeCore.h"
#include "RandomPayload.h"
#include "UiRect.h"

using namespace espbarcode;
using uigeom::Rect;

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

void test_random_payload_always_encodes() {
    static constexpr Symbology kAllTypes[] = {
        Symbology::QrCode, Symbology::DataMatrix, Symbology::Aztec,
        Symbology::Code128, Symbology::Gs1_128, Symbology::Code39,
        Symbology::Ean13, Symbology::Ean8, Symbology::UpcA,
        Symbology::Itf, Symbology::Itf14, Symbology::Codabar, Symbology::Msi
    };

    std::mt19937 rng(1234567);
    const std::function<uint32_t()> nextRandom = [&rng] { return rng(); };

    for (Symbology type : kAllTypes) {
        for (int attempt = 0; attempt < 25; ++attempt) {
            BarcodeSpec spec;
            spec.type = type;
            spec.data = randomValidPayload(type, nextRandom);
            const auto result = encode(spec);
            char message[160];
            std::snprintf(message, sizeof(message), "%s payload '%s' failed: %s",
                          toString(type), spec.data.c_str(), result.error.c_str());
            TEST_ASSERT_TRUE_MESSAGE(result.ok, message);
        }
    }
}

void test_home_button_layout_has_no_overlaps_and_fits_screen() {
    // Mirrors the home-screen button layout in BarcodeApplication.cpp.
    static constexpr Rect kButtons[] = {
        {8, 38, 150, 38},    // TYPE
        {166, 38, 70, 38},   // CLEAR
        {244, 38, 68, 38},   // SAVE
        {8, 176, 70, 38},    // OPTIONS
        {86, 176, 70, 38},   // PRESETS
        {164, 176, 70, 38},  // DISPLAY
        {242, 176, 70, 38},  // RANDOM
    };
    constexpr int16_t kScreenWidth = 320;
    constexpr std::size_t kCount = sizeof(kButtons) / sizeof(kButtons[0]);

    for (const Rect& r : kButtons) {
        TEST_ASSERT_TRUE(r.x >= 0);
        TEST_ASSERT_TRUE(r.x + r.w <= kScreenWidth);
    }
    for (std::size_t i = 0; i < kCount; ++i) {
        for (std::size_t j = i + 1; j < kCount; ++j) {
            TEST_ASSERT_FALSE_MESSAGE(uigeom::overlaps(kButtons[i], kButtons[j]), "home screen buttons overlap");
        }
    }
}

void test_touch_pad_closes_gap_without_crossing_neighbor() {
    // Regression test for the CLEAR button being unreachable in the narrow
    // 8px gap between TYPE and CLEAR (and CLEAR and SAVE).
    constexpr Rect kType{8, 38, 150, 38};
    constexpr Rect kClear{166, 38, 70, 38};
    constexpr Rect kSave{244, 38, 68, 38};
    constexpr int16_t pad = 4;

    // A touch in what used to be dead space now resolves to CLEAR, not TYPE.
    TEST_ASSERT_FALSE(kType.contains(163, 57, pad));
    TEST_ASSERT_TRUE(kClear.contains(163, 57, pad));

    // Touches well inside TYPE never bleed into CLEAR.
    TEST_ASSERT_TRUE(kType.contains(100, 57, pad));
    TEST_ASSERT_FALSE(kClear.contains(100, 57, pad));

    // The CLEAR/SAVE gap splits cleanly with no double-match on either side.
    TEST_ASSERT_TRUE(kClear.contains(239, 57, pad));
    TEST_ASSERT_FALSE(kSave.contains(239, 57, pad));
    TEST_ASSERT_FALSE(kClear.contains(240, 57, pad));
    TEST_ASSERT_TRUE(kSave.contains(240, 57, pad));
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
    RUN_TEST(test_random_payload_always_encodes);
    RUN_TEST(test_home_button_layout_has_no_overlaps_and_fits_screen);
    RUN_TEST(test_touch_pad_closes_gap_without_crossing_neighbor);
    return UNITY_END();
}
