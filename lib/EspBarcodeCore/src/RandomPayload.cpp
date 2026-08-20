#include "RandomPayload.h"

namespace espbarcode {
namespace {

std::size_t randomLengthInRange(const std::function<uint32_t()>& nextRandom,
                                 std::size_t minLen,
                                 std::size_t maxLen) {
    if (maxLen <= minLen) return minLen;
    return minLen + nextRandom() % (maxLen - minLen + 1);
}

std::string randomDigits(const std::function<uint32_t()>& nextRandom, std::size_t length) {
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) out.push_back(static_cast<char>('0' + nextRandom() % 10));
    return out;
}

std::string randomAlnum(const std::function<uint32_t()>& nextRandom, std::size_t length) {
    static const char kCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) out.push_back(kCharset[nextRandom() % (sizeof(kCharset) - 1)]);
    return out;
}

}  // namespace

std::string randomValidPayload(Symbology type, const std::function<uint32_t()>& nextRandom) {
    switch (type) {
        case Symbology::QrCode:
        case Symbology::DataMatrix:
        case Symbology::Aztec:
        case Symbology::Code128:
            return randomAlnum(nextRandom, randomLengthInRange(nextRandom, 8, 16));
        case Symbology::Gs1_128:
            return "{FNC1}01" + randomDigits(nextRandom, 14);
        case Symbology::Code39:
            return randomAlnum(nextRandom, randomLengthInRange(nextRandom, 6, 12));
        case Symbology::Ean13:
            return randomDigits(nextRandom, 12);
        case Symbology::Ean8:
            return randomDigits(nextRandom, 7);
        case Symbology::UpcA:
            return randomDigits(nextRandom, 11);
        case Symbology::Itf:
            return randomDigits(nextRandom, randomLengthInRange(nextRandom, 4, 7) * 2);
        case Symbology::Itf14:
            return randomDigits(nextRandom, 13);
        case Symbology::Codabar:
            return randomDigits(nextRandom, randomLengthInRange(nextRandom, 6, 10));
        case Symbology::Msi:
            return randomDigits(nextRandom, randomLengthInRange(nextRandom, 5, 9));
    }
    return randomAlnum(nextRandom, 10);
}

}  // namespace espbarcode
