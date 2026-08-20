#include "EspBarcodeCore.h"

// This encoder is a C++ adaptation of Alois Zingl's MIT-licensed Aztec
// reference implementation. See THIRD_PARTY_NOTICES.md.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace espbarcode {
namespace {

BarcodeResult aztecFailure(const std::string& message) {
    BarcodeResult out;
    out.error = message;
    return out;
}

using Sequence = std::vector<int>;

}  // namespace

BarcodeResult encodeAztec(const BarcodeSpec& spec) {
    // Compact adaptation of Alois Zingl's MIT-licensed Aztec encoder. The state
    // vector stores its bit length in element 0 followed by packed words.
    static constexpr int kInfinity = 20000;
    static constexpr int kCharSize[5] = {5, 5, 5, 5, 4};
    static constexpr int kLatchLength[6][6] = {
        {0, 5, 5, 10, 5, 10},
        {9, 0, 5, 10, 5, 10},
        {5, 5, 0, 5, 10, 10},
        {5, 10, 10, 0, 10, 15},
        {4, 9, 9, 14, 0, 14},
        {0, 0, 0, 0, 0, 0}
    };
    static constexpr int kShiftLength[5][5] = {
        {0, kInfinity, kInfinity, 5, kInfinity},
        {5, 0, kInfinity, 5, kInfinity},
        {kInfinity, kInfinity, 0, 5, kInfinity},
        {kInfinity, kInfinity, kInfinity, 0, kInfinity},
        {4, kInfinity, kInfinity, 4, 0}
    };
    static const std::vector<int> kLatch[5][6] = {
        {{}, {28}, {29}, {29, 30}, {30}, {31}},
        {{30, 14}, {}, {29}, {29, 30}, {30}, {31}},
        {{29}, {28}, {}, {30}, {28, 30}, {31}},
        {{31}, {31, 28}, {31, 29}, {}, {31, 30}, {31, 31}},
        {{14}, {14, 28}, {14, 29}, {14, 29, 30}, {}, {14, 31}}
    };

    static const std::array<std::string, 5> kCharMap = [] {
        std::array<std::string, 5> maps;
        maps[0] = "  ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        maps[1] = "  abcdefghijklmnopqrstuvwxyz";
        const unsigned char mixed[] = {
            0, 32, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
            27, 28, 29, 30, 31, 64, 92, 94, 95, 96, 124, 126, 127
        };
        maps[2].assign(reinterpret_cast<const char*>(mixed), sizeof(mixed));
        maps[3] = " \r\r\r\r\r!\"#$%&'()*+,-./:;<=>?[]{}";
        maps[4] = "  0123456789,.";
        return maps;
    }();

    const std::string& text = spec.data;
    const int inputLength = static_cast<int>(text.size());
    if (inputLength > 2048) return aztecFailure("Aztec payload exceeds 2048 bytes");

    const int minimumLayers = std::min<int>(spec.aztecMinLayers, 32);
    const int securityPercent = std::clamp<int>(spec.aztecSecurityPercent, 1, 90);
    const double securityFactor = 100.0 / static_cast<double>(100 - securityPercent);

    int symbolTypeForStuffing = 0;
    int wordBits = 0;
    int binaryBytes = 0;
    int backTo = 0;

    auto stream = [&](Sequence& sequence, int value, int bits) {
        int endBits = sequence[0] % wordBits + bits;
        uint32_t shifted = static_cast<uint32_t>(value) << wordBits;
        sequence[0] += bits;
        sequence.back() |= static_cast<int>(shifted >> endBits);
        while (endBits >= wordBits) {
            int top = sequence.back() >> 1;
            if (symbolTypeForStuffing == 0 &&
                (top == 0 || 2 * top + 2 == (1 << wordBits))) {
                sequence.back() = 2 * top + (((top & 1) ^ 1));
                ++sequence[0];
                ++endBits;
            }
            endBits -= wordBits;
            sequence.push_back(static_cast<int>((shifted >> endBits) &
                                                static_cast<uint32_t>((1 << wordBits) - 1)));
        }
    };

    auto binary = [&](Sequence& sequence, int position) {
        sequence[0] -= binaryBytes * 8 + (binaryBytes > 31 ? 16 : 5);
        stream(sequence, binaryBytes > 31 ? 0 : binaryBytes, 5);
        if (binaryBytes > 31) stream(sequence, binaryBytes - 31, 11);
        for (int i = position - binaryBytes; i < position; ++i) {
            stream(sequence, static_cast<uint8_t>(text[static_cast<std::size_t>(i)]), 8);
        }
    };

    int estimatedBits = 4;
    int priorWordBits = 4;
    int dataWordCount = 0;
    Sequence encoded;

    while (true) {
        estimatedBits = std::max(estimatedBits,
                                 (static_cast<int>(inputLength * securityFactor) + 3) * priorWordBits);
        wordBits = estimatedBits <= 240 ? 6
                 : estimatedBits <= 1920 ? 8
                 : estimatedBits <= 10208 ? 10
                 : 12;
        if (minimumLayers != 0) {
            const int layerBits = minimumLayers < 3 ? 6
                                : minimumLayers < 9 ? 8
                                : minimumLayers < 23 ? 10
                                : 12;
            wordBits = std::max(wordBits, layerBits);
        }
        if (priorWordBits >= wordBits) break;

        priorWordBits = wordBits;
        int position = 0;
        binaryBytes = 0;
        backTo = 0;
        std::array<Sequence, 6> current = {
            Sequence{0, 0}, Sequence{kInfinity, 0}, Sequence{kInfinity, 0},
            Sequence{kInfinity, 0}, Sequence{kInfinity, 0}, Sequence{kInfinity, 0}
        };

        while (position < inputLength) {
            for (int to = 0; to < 6; ++to) {
                for (int from = 0; from < 6; ++from) {
                    if (current[from][0] >= kInfinity) continue;
                    if (current[from][0] + kLatchLength[from][to] < current[to][0] &&
                        (from < 5 || to == backTo)) {
                        current[to] = current[from];
                        if (from < 5) {
                            for (int latch : kLatch[from][to]) {
                                stream(current[to], latch, latch < 16 ? 4 : 5);
                            }
                        } else {
                            binary(current[to], position);
                        }
                        if (to == 5) {
                            backTo = from;
                            binaryBytes = 0;
                            current[5][0] += 5;
                        }
                    }
                }
            }

            std::array<Sequence, 6> next = {
                Sequence{kInfinity}, Sequence{kInfinity}, Sequence{kInfinity},
                Sequence{kInfinity}, Sequence{kInfinity}, current[5]
            };

            int pairIndex = -1;
            if (position + 1 < inputLength) {
                const char first = text[static_cast<std::size_t>(position)];
                const char second = text[static_cast<std::size_t>(position + 1)];
                if (first == '\r' && second == '\n') pairIndex = 0;
                else if (first == '.' && second == ' ') pairIndex = 1;
                else if (first == ',' && second == ' ') pairIndex = 2;
                else if (first == ':' && second == ' ') pairIndex = 3;
            }

            for (int to = 0; to < 5; ++to) {
                int index = -1;
                if (pairIndex >= 0) {
                    if (to == 3) index = pairIndex + 2;
                } else {
                    const auto found = kCharMap[static_cast<std::size_t>(to)].find(
                        text[static_cast<std::size_t>(position)], 1);
                    if (found != std::string::npos) index = static_cast<int>(found);
                }
                if (index < 0) continue;

                for (int from = 0; from < 5; ++from) {
                    if (current[from][0] >= kInfinity) continue;
                    const int candidateLength = current[from][0] +
                                                kShiftLength[from][to] +
                                                kCharSize[to];
                    if (candidateLength < next[from][0]) {
                        next[from] = current[from];
                        if (to != from) {
                            const int shift = to == 3 ? 0 : (from < 4 ? 28 : 15);
                            stream(next[from], shift, kCharSize[from]);
                        }
                        stream(next[from], index, kCharSize[to]);
                    }
                }
            }

            next[5][0] += binaryBytes == 31 ? 19 : 8;
            ++binaryBytes;
            if (pairIndex >= 0) {
                ++position;
                next[5][0] += binaryBytes == 31 ? 19 : 8;
                ++binaryBytes;
            }
            ++position;
            current = std::move(next);
        }

        binary(current[5], inputLength);
        encoded = *std::min_element(current.begin(), current.end(),
                                    [](const Sequence& a, const Sequence& b) {
                                        return a[0] < b[0];
                                    });
        const int paddingBits = wordBits - encoded[0] % wordBits;
        if (paddingBits < wordBits) stream(encoded, (1 << paddingBits) - 1, paddingBits);
        if (encoded.size() < 2) return aztecFailure("Aztec high-level encoding failed");
        encoded.pop_back();  // remove trailing empty word
        dataWordCount = encoded[0] / wordBits;
        encoded.erase(encoded.begin());
    }

    if (dataWordCount > 1660) return aztecFailure("Aztec payload is too large");

    int finderDiameter = (estimatedBits > 608 || dataWordCount > 64) ? 14 : 11;
    int runeValue = -1;
    if (!text.empty() && text.size() <= 3 &&
        std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
        const int parsed = std::atoi(text.c_str());
        if (parsed >= 0 && parsed <= 255 && std::to_string(parsed) == text) runeValue = parsed;
    }

    int layers = minimumLayers;
    if (runeValue < 0 || layers != 0) {
        const int needed = static_cast<int>(std::ceil(
            (std::sqrt(static_cast<double>(estimatedBits + finderDiameter * finderDiameter)) -
             finderDiameter) / 4.0));
        layers = std::max(layers, std::min(32, needed));
    }
    if (layers < 0 || layers > 32) return aztecFailure("Aztec layer selection is invalid");

    const int errorWords = layers > 0
        ? (8 * layers * (finderDiameter + 2 * layers)) / wordBits - dataWordCount
        : 0;
    if (layers > 0 && errorWords < 3) return aztecFailure("Aztec error-correction allocation failed");

    int type = finderDiameter >> 1;  // 5 compact, 7 full
    int center = type + 2 * layers;
    center += (center + 14) / 15;  // reference grid expansion for full symbols

    auto reedSolomon = [&](int checkWords, int fieldSize, int polynomial) {
        std::vector<int> correction(static_cast<std::size_t>(checkWords + 2), 0);
        std::vector<int> log(static_cast<std::size_t>(fieldSize + 1), 0);
        std::vector<int> exp(static_cast<std::size_t>(fieldSize), 0);
        int value = 1;
        dataWordCount = static_cast<int>(encoded.size());
        for (int i = 0; i < fieldSize; ++i) {
            exp[static_cast<std::size_t>(i)] = value;
            log[static_cast<std::size_t>(value)] = i;
            value += value;
            if (value > fieldSize) value ^= polynomial;
        }
        for (int i = 0; i <= checkWords; ++i) {
            correction[static_cast<std::size_t>(checkWords - i)] = 1;
            for (int j = checkWords - i + 1; j <= checkWords; ++j) {
                const int a = correction[static_cast<std::size_t>(j + 1)];
                const int b = correction[static_cast<std::size_t>(j)];
                correction[static_cast<std::size_t>(j)] =
                    a ^ exp[static_cast<std::size_t>((log[static_cast<std::size_t>(b)] + i) % fieldSize)];
            }
            encoded.push_back(0);
        }
        for (int i = 0; i < dataWordCount; ++i) {
            const int x = encoded[static_cast<std::size_t>(dataWordCount)] ^
                          encoded[static_cast<std::size_t>(i)];
            for (int j = 0; j < checkWords; ++j) {
                const int product = x == 0
                    ? 0
                    : exp[static_cast<std::size_t>((
                        log[static_cast<std::size_t>(correction[static_cast<std::size_t>(j + 1)])] +
                        log[static_cast<std::size_t>(x)]) % fieldSize)];
                encoded[static_cast<std::size_t>(dataWordCount + j)] =
                    encoded[static_cast<std::size_t>(dataWordCount + j + 1)] ^ product;
            }
        }
    };

    BitMatrix matrix(static_cast<uint16_t>(2 * center + 1),
                     static_cast<uint16_t>(2 * center + 1));
    auto setRelative = [&](int x, int y) {
        const int mx = center + x;
        const int my = center + y;
        if (mx >= 0 && my >= 0 && mx < matrix.width() && my < matrix.height()) {
            matrix.set(static_cast<uint16_t>(mx), static_cast<uint16_t>(my));
        }
    };

    for (int y = 1 - type; y < type; ++y) {
        for (int x = 1 - type; x < type; ++x) {
            if ((1 ^ (std::max(std::abs(x), std::abs(y)) & 1)) != 0) setRelative(x, y);
        }
    }
    setRelative(-type, -type + 1);
    setRelative(-type, -type);
    setRelative(-type + 1, -type);
    setRelative(type, type - 1);
    setRelative(type, -type + 1);
    setRelative(type, -type);

    int x = 0;
    int y = 0;
    auto move = [&](int dx, int dy) {
        x += dx;
        if (type == 7 && (x & 15) == 0) x += dx;
        y += dy;
        if (type == 7 && (y & 15) == 0) y += dy;
    };

    int modeValue = runeValue;
    if (layers > 0) {
        static constexpr int kPolynomials[4] = {67, 301, 1033, 4201};
        reedSolomon(errorWords,
                    (1 << wordBits) - 1,
                    kPolynomials[wordBits / 2 - 3]);
        if (encoded.empty()) return aztecFailure("Aztec error-correction stream is empty");
        encoded.pop_back();  // remove terminal register word

        x = -type;
        y = x - 1;
        int sideProgress = (3 * type + 9) / 2;
        int sideLength = sideProgress;
        int dx = 1;
        int dy = 0;
        while (!encoded.empty()) {
            int word = encoded.back();
            encoded.pop_back();
            for (int i = 0; i < wordBits / 2; ++i) {
                if ((word & 1) != 0) setRelative(x, y);
                move(dy, -dx);
                if ((word & 2) != 0) setRelative(x, y);
                move(dx - dy, dx + dy);
                --sideProgress;
                word >>= 2;
                if (sideProgress < 0) {
                    move(dy, -dx);
                    const int oldDx = dx;
                    dx = -dy;
                    dy = oldDx;
                    if (dx < 1) {
                        move(dx - dy, dx + dy);
                        move(dx - dy, dx + dy);
                    } else {
                        sideLength += 4;
                    }
                    sideProgress = sideLength;
                }
            }
        }

        if (type == 7) {
            for (int gx = ((16 - center) & -16); gx < center; gx += 16) {
                for (int gy = ((2 - center) & -2); gy < center; gy += 2) {
                    if (std::abs(gx) > type || std::abs(gy) > type) setRelative(gx, gy);
                    if ((gy & 15) != 0) setRelative(gy, gx);
                }
            }
        }
        modeValue = (layers - 1) * (type * 992 - 4896) + dataWordCount - 1;
    }

    encoded.assign(static_cast<std::size_t>(type - 2), 0);
    for (int i = type - 3; i >= 0; --i) {
        encoded[static_cast<std::size_t>(i)] = modeValue & 15;
        modeValue >>= 4;
    }
    reedSolomon((type + 5) / 2, 15, 19);

    wordBits = (type * 3 - 1) / 2;
    int bitMask = 1;
    const int runeXor = layers > 0 ? 0 : 10;
    for (int i = 0; i < wordBits; ++i) {
        stream(encoded,
               runeXor ^ encoded[static_cast<std::size_t>(i + 1)],
               4);
    }
    for (int i = 2 - type; i < type - 1; ++i) {
        if (type == 7 && i == 0) continue;
        if ((encoded[static_cast<std::size_t>(wordBits + 1)] & bitMask) != 0) setRelative(-i, -type);
        if ((encoded[static_cast<std::size_t>(wordBits + 2)] & bitMask) != 0) setRelative(type, -i);
        if ((encoded[static_cast<std::size_t>(wordBits + 3)] & bitMask) != 0) setRelative(i, type);
        if ((encoded[static_cast<std::size_t>(wordBits + 4)] & bitMask) != 0) setRelative(-type, i);
        bitMask += bitMask;
    }

    BarcodeResult result;
    result.ok = true;
    result.matrix = std::move(matrix);
    result.normalizedData = spec.data;
    result.linear = false;
    result.defaultQuietZone = 2;
    return result;
}

}  // namespace espbarcode
