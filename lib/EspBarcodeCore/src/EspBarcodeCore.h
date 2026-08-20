#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace espbarcode {

enum class Symbology : uint8_t {
    QrCode,
    DataMatrix,
    Aztec,
    Code128,
    Gs1_128,
    Code39,
    Ean13,
    Ean8,
    UpcA,
    Itf,
    Itf14,
    Codabar,
    Msi
};

enum class ErrorCorrection : uint8_t { Low, Medium, Quartile, High };
enum class Rotation : uint16_t { Deg0 = 0, Deg90 = 90, Deg180 = 180, Deg270 = 270, Auto = 65535 };

struct BarcodeSpec {
    Symbology type = Symbology::QrCode;
    std::string data;
    ErrorCorrection ecc = ErrorCorrection::Medium;
    Rotation rotation = Rotation::Auto;
    int quietZone = -1;
    uint8_t minModulePixels = 2;
    uint8_t qrMinVersion = 1;
    uint8_t qrMaxVersion = 20;
    uint8_t aztecSecurityPercent = 23;
    uint8_t aztecMinLayers = 1;
    bool dataMatrixRectangular = false;
    bool invert = false;
    bool checksum = true;
};

class BitMatrix {
public:
    BitMatrix() = default;
    BitMatrix(uint16_t width, uint16_t height);

    bool resize(uint16_t width, uint16_t height);
    void clear();
    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    bool empty() const { return width_ == 0 || height_ == 0; }
    bool get(uint16_t x, uint16_t y) const;
    void set(uint16_t x, uint16_t y, bool value = true);
    const std::vector<uint8_t>& packed() const { return bits_; }
    std::vector<uint8_t>& packed() { return bits_; }

private:
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    std::vector<uint8_t> bits_;
};

struct BarcodeResult {
    bool ok = false;
    BitMatrix matrix;
    std::string normalizedData;
    std::string error;
    bool linear = false;
    uint8_t defaultQuietZone = 4;
};

struct RenderLayout {
    bool ok = false;
    Rotation rotation = Rotation::Deg0;
    uint16_t modulePixels = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t widthPixels = 0;
    uint16_t heightPixels = 0;
    uint16_t quietModules = 0;
    uint16_t logicalWidth = 0;
    uint16_t logicalHeight = 0;
    std::string error;
};

const char* toString(Symbology type);
bool tryParseSymbology(const std::string& text, Symbology& out);
const char* toString(ErrorCorrection ecc);
bool tryParseErrorCorrection(const std::string& text, ErrorCorrection& out);
const char* toString(Rotation rotation);
bool tryParseRotation(const std::string& text, Rotation& out);

BarcodeResult encode(const BarcodeSpec& spec);
BarcodeResult encodeDataMatrix(const BarcodeSpec& spec);
BarcodeResult encodeAztec(const BarcodeSpec& spec);
BarcodeResult encodeCode128(const BarcodeSpec& spec, bool gs1 = false);
BarcodeResult encodeCode39(const BarcodeSpec& spec);
BarcodeResult encodeEan13(const BarcodeSpec& spec);
BarcodeResult encodeEan8(const BarcodeSpec& spec);
BarcodeResult encodeUpcA(const BarcodeSpec& spec);
BarcodeResult encodeItf(const BarcodeSpec& spec, bool itf14 = false);
BarcodeResult encodeCodabar(const BarcodeSpec& spec);
BarcodeResult encodeMsi(const BarcodeSpec& spec);
BarcodeResult encodeQr(const BarcodeSpec& spec);

RenderLayout calculateLayout(const BitMatrix& matrix,
                             bool linear,
                             uint16_t screenWidth,
                             uint16_t screenHeight,
                             Rotation requested,
                             uint16_t quietModules,
                             uint8_t minimumModulePixels);

std::string bytesToBase64(const std::vector<uint8_t>& bytes);
bool bytesFromBase64(const std::string& encoded, std::vector<uint8_t>& out);
std::string packedToBase64(const BitMatrix& matrix);
bool packedFromBase64(uint16_t width, uint16_t height, const std::string& encoded, BitMatrix& out);

}  // namespace espbarcode
