#include "EspBarcodeCore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace espbarcode {

BitMatrix::BitMatrix(uint16_t width, uint16_t height) { resize(width, height); }

bool BitMatrix::resize(uint16_t width, uint16_t height) {
    const std::size_t bitCount = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || bitCount > 512U * 512U) {
        width_ = height_ = 0;
        bits_.clear();
        return false;
    }
    width_ = width;
    height_ = height;
    bits_.assign((bitCount + 7U) / 8U, 0);
    return true;
}

void BitMatrix::clear() { std::fill(bits_.begin(), bits_.end(), 0); }

bool BitMatrix::get(uint16_t x, uint16_t y) const {
    if (x >= width_ || y >= height_) return false;
    const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
    return (bits_[index >> 3U] & static_cast<uint8_t>(1U << (7U - (index & 7U)))) != 0;
}

void BitMatrix::set(uint16_t x, uint16_t y, bool value) {
    if (x >= width_ || y >= height_) return;
    const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
    const uint8_t mask = static_cast<uint8_t>(1U << (7U - (index & 7U)));
    if (value) bits_[index >> 3U] |= mask;
    else bits_[index >> 3U] &= static_cast<uint8_t>(~mask);
}

const char* toString(Symbology type) {
    switch (type) {
        case Symbology::QrCode: return "qr";
        case Symbology::DataMatrix: return "datamatrix";
        case Symbology::Aztec: return "aztec";
        case Symbology::Code128: return "code128";
        case Symbology::Gs1_128: return "gs1-128";
        case Symbology::Code39: return "code39";
        case Symbology::Ean13: return "ean13";
        case Symbology::Ean8: return "ean8";
        case Symbology::UpcA: return "upca";
        case Symbology::Itf: return "itf";
        case Symbology::Itf14: return "itf14";
        case Symbology::Codabar: return "codabar";
        case Symbology::Msi: return "msi";
    }
    return "unknown";
}

static std::string canonical(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool tryParseSymbology(const std::string& text, Symbology& out) {
    const std::string v = canonical(text);
    if (v == "qr" || v == "qrcode") out = Symbology::QrCode;
    else if (v == "datamatrix" || v == "dm") out = Symbology::DataMatrix;
    else if (v == "aztec") out = Symbology::Aztec;
    else if (v == "code128" || v == "c128") out = Symbology::Code128;
    else if (v == "gs1128" || v == "ean128") out = Symbology::Gs1_128;
    else if (v == "code39" || v == "c39") out = Symbology::Code39;
    else if (v == "ean13") out = Symbology::Ean13;
    else if (v == "ean8") out = Symbology::Ean8;
    else if (v == "upca" || v == "upc") out = Symbology::UpcA;
    else if (v == "itf" || v == "i2of5" || v == "interleaved2of5") out = Symbology::Itf;
    else if (v == "itf14") out = Symbology::Itf14;
    else if (v == "codabar" || v == "nw7") out = Symbology::Codabar;
    else if (v == "msi" || v == "msiplessey") out = Symbology::Msi;
    else return false;
    return true;
}

const char* toString(ErrorCorrection ecc) {
    switch (ecc) {
        case ErrorCorrection::Low: return "L";
        case ErrorCorrection::Medium: return "M";
        case ErrorCorrection::Quartile: return "Q";
        case ErrorCorrection::High: return "H";
    }
    return "M";
}

bool tryParseErrorCorrection(const std::string& text, ErrorCorrection& out) {
    const std::string value = canonical(text);
    if (value == "l" || value == "low") out = ErrorCorrection::Low;
    else if (value == "m" || value == "medium") out = ErrorCorrection::Medium;
    else if (value == "q" || value == "quartile") out = ErrorCorrection::Quartile;
    else if (value == "h" || value == "high") out = ErrorCorrection::High;
    else return false;
    return true;
}

const char* toString(Rotation rotation) {
    switch (rotation) {
        case Rotation::Deg0: return "0";
        case Rotation::Deg90: return "90";
        case Rotation::Deg180: return "180";
        case Rotation::Deg270: return "270";
        case Rotation::Auto: return "auto";
    }
    return "auto";
}

bool tryParseRotation(const std::string& text, Rotation& out) {
    const std::string value = canonical(text);
    if (value == "0" || value == "deg0") out = Rotation::Deg0;
    else if (value == "90" || value == "deg90") out = Rotation::Deg90;
    else if (value == "180" || value == "deg180") out = Rotation::Deg180;
    else if (value == "270" || value == "deg270") out = Rotation::Deg270;
    else if (value == "auto") out = Rotation::Auto;
    else return false;
    return true;
}

BarcodeResult encode(const BarcodeSpec& spec) {
    if (spec.data.size() > 2048) return {false, {}, {}, "payload exceeds 2048 bytes", false, 0};
    switch (spec.type) {
        case Symbology::QrCode: return encodeQr(spec);
        case Symbology::DataMatrix: return encodeDataMatrix(spec);
        case Symbology::Aztec: return encodeAztec(spec);
        case Symbology::Code128: return encodeCode128(spec, false);
        case Symbology::Gs1_128: return encodeCode128(spec, true);
        case Symbology::Code39: return encodeCode39(spec);
        case Symbology::Ean13: return encodeEan13(spec);
        case Symbology::Ean8: return encodeEan8(spec);
        case Symbology::UpcA: return encodeUpcA(spec);
        case Symbology::Itf: return encodeItf(spec, false);
        case Symbology::Itf14: return encodeItf(spec, true);
        case Symbology::Codabar: return encodeCodabar(spec);
        case Symbology::Msi: return encodeMsi(spec);
    }
    return {false, {}, {}, "unsupported symbology", false, 0};
}

static RenderLayout candidate(const BitMatrix& matrix,
                              bool linear,
                              uint16_t screenWidth,
                              uint16_t screenHeight,
                              Rotation rotation,
                              uint16_t quiet,
                              uint8_t minimum) {
    RenderLayout result;
    result.rotation = rotation;
    result.quietModules = quiet;

    uint32_t contentW = matrix.width();
    uint32_t contentH = linear ? 48U : matrix.height();
    if (rotation == Rotation::Deg90 || rotation == Rotation::Deg270) std::swap(contentW, contentH);

    const uint32_t logicalW = contentW + 2U * quiet;
    const uint32_t logicalH = contentH + 2U * quiet;
    result.logicalWidth = static_cast<uint16_t>(std::min<uint32_t>(logicalW, 65535));
    result.logicalHeight = static_cast<uint16_t>(std::min<uint32_t>(logicalH, 65535));
    if (logicalW == 0 || logicalH == 0) {
        result.error = "empty symbol";
        return result;
    }

    const uint32_t scale = std::min<uint32_t>(screenWidth / logicalW, screenHeight / logicalH);
    if (scale < minimum || scale == 0) {
        result.error = "symbol is too dense for the display at the requested minimum module size";
        return result;
    }

    result.modulePixels = static_cast<uint16_t>(scale);
    result.widthPixels = static_cast<uint16_t>(logicalW * scale);
    result.heightPixels = static_cast<uint16_t>(logicalH * scale);
    result.x = static_cast<uint16_t>((screenWidth - result.widthPixels) / 2U);
    result.y = static_cast<uint16_t>((screenHeight - result.heightPixels) / 2U);
    result.ok = true;
    return result;
}

RenderLayout calculateLayout(const BitMatrix& matrix,
                             bool linear,
                             uint16_t screenWidth,
                             uint16_t screenHeight,
                             Rotation requested,
                             uint16_t quietModules,
                             uint8_t minimumModulePixels) {
    if (matrix.empty()) return {false, Rotation::Deg0, 0, 0, 0, 0, 0, quietModules, 0, 0, "empty symbol"};
    minimumModulePixels = std::max<uint8_t>(minimumModulePixels, 1);

    if (requested != Rotation::Auto) {
        return candidate(matrix, linear, screenWidth, screenHeight, requested, quietModules, minimumModulePixels);
    }

    const auto zero = candidate(matrix, linear, screenWidth, screenHeight, Rotation::Deg0, quietModules, minimumModulePixels);
    const auto ninety = candidate(matrix, linear, screenWidth, screenHeight, Rotation::Deg90, quietModules, minimumModulePixels);
    if (zero.ok && ninety.ok) return ninety.modulePixels > zero.modulePixels ? ninety : zero;
    if (zero.ok) return zero;
    if (ninety.ok) return ninety;
    return zero.modulePixels >= ninety.modulePixels ? zero : ninety;
}

static const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string bytesToBase64(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = bytes[i];
        const uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t v = (a << 16U) | (b << 8U) | c;
        out.push_back(kBase64[(v >> 18U) & 63U]);
        out.push_back(kBase64[(v >> 12U) & 63U]);
        out.push_back(i + 1 < bytes.size() ? kBase64[(v >> 6U) & 63U] : '=');
        out.push_back(i + 2 < bytes.size() ? kBase64[v & 63U] : '=');
    }
    return out;
}

bool bytesFromBase64(const std::string& encoded, std::vector<uint8_t>& out) {
    if (encoded.size() % 4U != 0U) return false;
    std::vector<uint8_t> decoded;
    decoded.reserve((encoded.size() / 4U) * 3U);
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const int a = base64Value(encoded[i]);
        const int b = base64Value(encoded[i + 1]);
        const int c = encoded[i + 2] == '=' ? 0 : base64Value(encoded[i + 2]);
        const int d = encoded[i + 3] == '=' ? 0 : base64Value(encoded[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        if (encoded[i + 2] == '=' && encoded[i + 3] != '=') return false;
        if ((encoded[i + 2] == '=' || encoded[i + 3] == '=') && i + 4U != encoded.size()) return false;
        const uint32_t v = (static_cast<uint32_t>(a) << 18U) |
                           (static_cast<uint32_t>(b) << 12U) |
                           (static_cast<uint32_t>(c) << 6U) |
                           static_cast<uint32_t>(d);
        decoded.push_back(static_cast<uint8_t>((v >> 16U) & 0xFFU));
        if (encoded[i + 2] != '=') decoded.push_back(static_cast<uint8_t>((v >> 8U) & 0xFFU));
        if (encoded[i + 3] != '=') decoded.push_back(static_cast<uint8_t>(v & 0xFFU));
    }
    out = std::move(decoded);
    return true;
}

std::string packedToBase64(const BitMatrix& matrix) {
    return bytesToBase64(matrix.packed());
}

bool packedFromBase64(uint16_t width, uint16_t height, const std::string& encoded, BitMatrix& out) {
    if (!out.resize(width, height)) return false;
    std::vector<uint8_t> decoded;
    if (!bytesFromBase64(encoded, decoded) || decoded.size() != out.packed().size()) {
        out = BitMatrix{};
        return false;
    }
    out.packed() = std::move(decoded);
    return true;
}

}  // namespace espbarcode
