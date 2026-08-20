#include "EspBarcodeCore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace espbarcode {
namespace {

BarcodeResult failure(const std::string& message) {
    BarcodeResult out;
    out.error = message;
    return out;
}

BarcodeResult linearResult(const std::vector<uint8_t>& bits, const std::string& normalized, uint8_t quiet = 10) {
    BarcodeResult out;
    if (bits.empty() || bits.size() > 4096) return failure("linear symbol is empty or too wide");
    out.matrix.resize(static_cast<uint16_t>(bits.size()), 1);
    for (uint16_t x = 0; x < bits.size(); ++x) out.matrix.set(x, 0, bits[x] != 0);
    out.ok = true;
    out.linear = true;
    out.defaultQuietZone = quiet;
    out.normalizedData = normalized;
    return out;
}

bool allDigits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

int checkDigit(const std::string& body) {
    int sum = 0;
    int weight = 3;
    for (auto it = body.rbegin(); it != body.rend(); ++it) {
        sum += (*it - '0') * weight;
        weight = weight == 3 ? 1 : 3;
    }
    return (10 - (sum % 10)) % 10;
}

bool normalizeWithCheck(const std::string& input, std::size_t bodyLength, std::string& normalized, std::string& error) {
    if (!allDigits(input)) {
        error = "payload must contain digits only";
        return false;
    }
    if (input.size() == bodyLength) {
        normalized = input + static_cast<char>('0' + checkDigit(input));
        return true;
    }
    if (input.size() == bodyLength + 1) {
        if (checkDigit(input.substr(0, bodyLength)) != input.back() - '0') {
            error = "invalid check digit";
            return false;
        }
        normalized = input;
        return true;
    }
    error = "payload has the wrong length";
    return false;
}

void appendBits(std::vector<uint8_t>& out, const char* bits) {
    for (const char* p = bits; *p; ++p) out.push_back(*p == '1');
}

void appendWidths(std::vector<uint8_t>& out, const std::string& widths, bool startBlack = true) {
    bool black = startBlack;
    for (char ch : widths) {
        const int width = ch - '0';
        for (int i = 0; i < width; ++i) out.push_back(black ? 1 : 0);
        black = !black;
    }
}

void appendWideNarrow(std::vector<uint8_t>& out, const std::string& pattern, int wide = 3) {
    for (char token : pattern) {
        const bool black = token == 'b' || token == 'B';
        const int width = (token == 'B' || token == 'S') ? wide : 1;
        for (int i = 0; i < width; ++i) out.push_back(black ? 1 : 0);
    }
}

std::string replaceFnc1Tokens(const std::string& input) {
    std::string out;
    for (std::size_t i = 0; i < input.size();) {
        if (input.compare(i, 6, "<FNC1>") == 0 || input.compare(i, 6, "{FNC1}") == 0) {
            out.push_back(static_cast<char>(29));
            i += 6;
        } else if (input.compare(i, 4, "<GS>") == 0) {
            out.push_back(static_cast<char>(29));
            i += 4;
        } else if (input.compare(i, 4, "\\x1D") == 0 || input.compare(i, 4, "\\x1d") == 0) {
            out.push_back(static_cast<char>(29));
            i += 4;
        } else {
            out.push_back(input[i++]);
        }
    }
    return out;
}

// ArduinoJson only escapes the named JSON control sequences (", \, \b, \f, \n,
// \r, \t); a raw 0x1D group separator would reach the wire unescaped and break
// strict JSON parsers. Substitute a printable token before it is echoed back
// in a response field.
std::string toReportableText(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (c == 29) {
            out += "<GS>";
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

int digitRun(const std::string& bytes, std::size_t at) {
    int count = 0;
    while (at + static_cast<std::size_t>(count) < bytes.size() &&
           bytes[at + static_cast<std::size_t>(count)] >= '0' &&
           bytes[at + static_cast<std::size_t>(count)] <= '9') ++count;
    return count;
}

void emitCode128Byte(std::vector<int>& words, int& set, unsigned char byte) {
    const bool extended = byte >= 128;
    unsigned char base = static_cast<unsigned char>(byte & 0x7F);

    if (set == 0 && base > 95) { words.push_back(100); set = 1; }
    if (set == 1 && base < 32) { words.push_back(101); set = 0; }

    if (extended) words.push_back(set == 0 ? 101 : 100);  // FNC4 single-character upper shift
    if (set == 0) words.push_back(base < 32 ? base + 64 : base - 32);
    else words.push_back(base - 32);
}

}  // namespace

BarcodeResult encodeCode128(const BarcodeSpec& spec, bool gs1) {
    const std::string bytes = replaceFnc1Tokens(spec.data);
    std::vector<int> words;
    std::size_t i = 0;
    int set = 1;  // 0=A, 1=B, 2=C

    const int firstDigits = digitRun(bytes, 0);
    if (firstDigits >= 4 || (firstDigits >= 2 && static_cast<std::size_t>(firstDigits) == bytes.size())) {
        set = 2;
        words.push_back(105);
    } else if (!bytes.empty() && static_cast<unsigned char>(bytes[0] & 0x7F) < 32) {
        set = 0;
        words.push_back(103);
    } else {
        set = 1;
        words.push_back(104);
    }
    if (gs1) words.push_back(102);

    while (i < bytes.size()) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (gs1 && c == 29) {
            words.push_back(102);
            ++i;
            continue;
        }

        if (set == 2) {
            if (i + 1 < bytes.size() && std::isdigit(static_cast<unsigned char>(bytes[i])) &&
                std::isdigit(static_cast<unsigned char>(bytes[i + 1]))) {
                words.push_back((bytes[i] - '0') * 10 + bytes[i + 1] - '0');
                i += 2;
                continue;
            }
            const unsigned char base = static_cast<unsigned char>(c & 0x7F);
            set = base < 32 ? 0 : 1;
            words.push_back(set == 0 ? 101 : 100);
            continue;
        }

        const int run = digitRun(bytes, i);
        const bool numericWorthwhile = run >= 4 || (run >= 2 && i + static_cast<std::size_t>(run) == bytes.size());
        if (numericWorthwhile) {
            if ((run & 1) != 0) {
                emitCode128Byte(words, set, c);
                ++i;
            } else {
                words.push_back(99);
                set = 2;
            }
            continue;
        }

        emitCode128Byte(words, set, c);
        ++i;
    }

    int checksum = words.front();
    for (std::size_t p = 1; p < words.size(); ++p) checksum += static_cast<int>(p) * words[p];
    words.push_back(checksum % 103);
    words.push_back(106);

    static const char* kPatterns[107] = {
        "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213",
        "221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
        "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211",
        "212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
        "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331",
        "231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
        "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214",
        "112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
        "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141",
        "214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
        "114131","311141","411131","211412","211214","211232","2331112"
    };

    std::vector<uint8_t> bits;
    bits.reserve(words.size() * 11U + 2U);
    for (int word : words) {
        if (word < 0 || word > 106) return failure("internal Code 128 codeword error");
        appendWidths(bits, kPatterns[word]);
    }
    return linearResult(bits, gs1 ? toReportableText(bytes) : spec.data, 10);
}

BarcodeResult encodeEan13(const BarcodeSpec& spec) {
    std::string digits, error;
    if (!normalizeWithCheck(spec.data, 12, digits, error)) return failure("EAN-13: " + error);

    static const char* L[10] = {"0001101","0011001","0010011","0111101","0100011","0110001","0101111","0111011","0110111","0001011"};
    static const char* G[10] = {"0100111","0110011","0011011","0100001","0011101","0111001","0000101","0010001","0001001","0010111"};
    static const char* R[10] = {"1110010","1100110","1101100","1000010","1011100","1001110","1010000","1000100","1001000","1110100"};
    static const char* parity[10] = {"LLLLLL","LLGLGG","LLGGLG","LLGGGL","LGLLGG","LGGLLG","LGGGLL","LGLGLG","LGLGGL","LGGLGL"};

    std::vector<uint8_t> bits;
    appendBits(bits, "101");
    const int first = digits[0] - '0';
    for (int i = 1; i <= 6; ++i) appendBits(bits, parity[first][i - 1] == 'L' ? L[digits[i] - '0'] : G[digits[i] - '0']);
    appendBits(bits, "01010");
    for (int i = 7; i < 13; ++i) appendBits(bits, R[digits[i] - '0']);
    appendBits(bits, "101");
    return linearResult(bits, digits, 10);
}

BarcodeResult encodeEan8(const BarcodeSpec& spec) {
    std::string digits, error;
    if (!normalizeWithCheck(spec.data, 7, digits, error)) return failure("EAN-8: " + error);
    static const char* L[10] = {"0001101","0011001","0010011","0111101","0100011","0110001","0101111","0111011","0110111","0001011"};
    static const char* R[10] = {"1110010","1100110","1101100","1000010","1011100","1001110","1010000","1000100","1001000","1110100"};
    std::vector<uint8_t> bits;
    appendBits(bits, "101");
    for (int i = 0; i < 4; ++i) appendBits(bits, L[digits[i] - '0']);
    appendBits(bits, "01010");
    for (int i = 4; i < 8; ++i) appendBits(bits, R[digits[i] - '0']);
    appendBits(bits, "101");
    return linearResult(bits, digits, 10);
}

BarcodeResult encodeUpcA(const BarcodeSpec& spec) {
    std::string digits, error;
    if (!normalizeWithCheck(spec.data, 11, digits, error)) return failure("UPC-A: " + error);
    BarcodeSpec ean = spec;
    ean.data = "0" + digits;
    ean.type = Symbology::Ean13;
    auto result = encodeEan13(ean);
    result.normalizedData = digits;
    return result;
}

BarcodeResult encodeItf(const BarcodeSpec& spec, bool itf14) {
    std::string digits = spec.data;
    if (!allDigits(digits)) return failure("ITF payload must contain digits only");
    if (itf14) {
        std::string normalized, error;
        if (!normalizeWithCheck(digits, 13, normalized, error)) return failure("ITF-14: " + error);
        digits = normalized;
    } else if ((digits.size() & 1U) != 0U) {
        digits.insert(digits.begin(), '0');
    }
    if (digits.size() > 80) return failure("ITF payload is too long");

    static const char* pattern[10] = {"11331","31113","13113","33111","11313","31311","13311","11133","31131","13131"};
    std::vector<uint8_t> bits;
    appendWidths(bits, "1111");
    for (std::size_t i = 0; i < digits.size(); i += 2) {
        const char* bars = pattern[digits[i] - '0'];
        const char* spaces = pattern[digits[i + 1] - '0'];
        for (int p = 0; p < 5; ++p) {
            for (int n = 0; n < bars[p] - '0'; ++n) bits.push_back(1);
            for (int n = 0; n < spaces[p] - '0'; ++n) bits.push_back(0);
        }
    }
    appendWidths(bits, "311");
    return linearResult(bits, digits, 10);
}

BarcodeResult encodeCode39(const BarcodeSpec& spec) {
    static const std::map<char, std::string> patterns = {
        {'0',"bsbSBsBsb"},{'1',"BsbSbsbsB"},{'2',"bsBSbsbsB"},{'3',"BsBSbsbsb"},{'4',"bsbSBsbsB"},
        {'5',"BsbSBsbsb"},{'6',"bsBSBsbsb"},{'7',"bsbSbsBsB"},{'8',"BsbSbsBsb"},{'9',"bsBSbsBsb"},
        {'A',"BsbsbSbsB"},{'B',"bsBsbSbsB"},{'C',"BsBsbSbsb"},{'D',"bsbsBSbsB"},{'E',"BsbsBSbsb"},
        {'F',"bsBsBSbsb"},{'G',"bsbsbSBsB"},{'H',"BsbsbSBsb"},{'I',"bsBsbSBsb"},{'J',"bsbsBSBsb"},
        {'K',"BsbsbsbSB"},{'L',"bsBsbsbSB"},{'M',"BsBsbsbSb"},{'N',"bsbsBsbSB"},{'O',"BsbsBsbSb"},
        {'P',"bsBsBsbSb"},{'Q',"bsbsbsBSB"},{'R',"BsbsbsBSb"},{'S',"bsBsbsBSb"},{'T',"bsbsBsBSb"},
        {'U',"BSbsbsbsB"},{'V',"bSBsbsbsB"},{'W',"BSBsbsbsb"},{'X',"bSbsBsbsB"},{'Y',"BSbsBsbsb"},
        {'Z',"bSBsBsbsb"},{'-',"bSbsbsBsB"},{'.',"BSbsbsBsb"},{' ',"bSBsbsBsb"},{'*',"bSbsBsBsb"},
        {'$',"bSbSbSbsb"},{'/',"bSbSbsbSb"},{'+',"bSbsbSbSb"},{'%',"bsbSbSbSb"}
    };
    std::string value;
    value.reserve(spec.data.size());
    for (unsigned char c : spec.data) {
        const char u = static_cast<char>(std::toupper(c));
        if (patterns.find(u) == patterns.end() || u == '*') return failure("Code 39 supports 0-9, A-Z, space, - . $ / + %");
        value.push_back(u);
    }
    if (value.size() > 80) return failure("Code 39 payload is too long");
    std::string encoded = "*" + value + "*";
    std::vector<uint8_t> bits;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        appendWideNarrow(bits, patterns.at(encoded[i]), 3);
        if (i + 1 < encoded.size()) bits.push_back(0);
    }
    return linearResult(bits, value, 10);
}

BarcodeResult encodeCodabar(const BarcodeSpec& spec) {
    static const std::map<char, std::string> patterns = {
        {'0',"bsbsbSB"},{'1',"bsbsBSb"},{'2',"bsbSbsB"},{'3',"BSbsbsb"},{'4',"bsBsbSb"},
        {'5',"BsbsbSb"},{'6',"bSbsbsB"},{'7',"bSbsBsb"},{'8',"bSBsbsb"},{'9',"BsbSbsb"},
        {'-',"bsbSBsb"},{'$',"bsBSbsb"},{':',"BsbsBsB"},{'/',"BsBsbsB"},{'.',"BsBsBsb"},
        {'+',"bsBsBsB"},{'A',"bsBSbSb"},{'B',"bSbSbsB"},{'C',"bsbSbSB"},{'D',"bsbSBSb"}
    };
    std::string value;
    for (unsigned char c : spec.data) value.push_back(static_cast<char>(std::toupper(c)));
    if (value.empty() || std::string("ABCD").find(value.front()) == std::string::npos) value.insert(value.begin(), 'A');
    if (std::string("ABCD").find(value.back()) == std::string::npos) value.push_back(value.front());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (patterns.find(value[i]) == patterns.end()) return failure("Codabar supports digits, - $ : / . + and A-D start/stop");
        if (i > 0 && i + 1 < value.size() && value[i] >= 'A' && value[i] <= 'D') return failure("Codabar start/stop characters may only appear at the ends");
    }
    std::vector<uint8_t> bits;
    for (std::size_t i = 0; i < value.size(); ++i) {
        appendWideNarrow(bits, patterns.at(value[i]), 3);
        if (i + 1 < value.size()) bits.push_back(0);
    }
    return linearResult(bits, value, 10);
}

BarcodeResult encodeMsi(const BarcodeSpec& spec) {
    if (!allDigits(spec.data)) return failure("MSI payload must contain digits only");
    std::string digits = spec.data;
    if (spec.checksum) {
        std::string doubled;
        for (std::size_t i = (digits.size() + 1U) % 2U; i < digits.size(); i += 2) doubled.push_back(digits[i]);
        unsigned long long n = 0;
        for (char c : doubled) n = n * 10ULL + static_cast<unsigned long long>(c - '0');
        n *= 2ULL;
        int total = 0;
        while (n > 0) { total += static_cast<int>(n % 10ULL); n /= 10ULL; }
        for (std::size_t i = digits.size() % 2U; i < digits.size(); i += 2) total += digits[i] - '0';
        digits.push_back(static_cast<char>('0' + ((10 - total % 10) % 10)));
    }
    static const char* patterns[10] = {"bSbSbSbS","bSbSbSBs","bSbSBsbS","bSbSBsBs","bSBsbSbS","bSBsbSBs","bSBsBsbS","bSBsBsBs","BsbSbSbS","BsbSbSBs"};
    std::vector<uint8_t> bits;
    appendWideNarrow(bits, "Bs", 2);
    for (char c : digits) appendWideNarrow(bits, patterns[c - '0'], 2);
    appendWideNarrow(bits, "bSb", 2);
    return linearResult(bits, digits, 10);
}

}  // namespace espbarcode
