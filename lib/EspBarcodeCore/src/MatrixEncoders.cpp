#include "EspBarcodeCore.h"

// The Data Matrix ECC200 placement and Reed-Solomon implementation in this
// file is a clean C++ adaptation of ideas from Alois Zingl's MIT-licensed
// 2D-Barcode reference implementation. See THIRD_PARTY_NOTICES.md.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace espbarcode {
namespace {

BarcodeResult matrixFailure(const std::string& message) {
    BarcodeResult out;
    out.error = message;
    return out;
}

BarcodeResult matrixResult(BitMatrix&& matrix,
                           const std::string& normalized,
                           uint8_t quietZone) {
    BarcodeResult out;
    out.ok = !matrix.empty();
    out.matrix = std::move(matrix);
    out.normalizedData = normalized;
    out.defaultQuietZone = quietZone;
    out.linear = false;
    if (!out.ok) out.error = "matrix encoder produced an empty symbol";
    return out;
}

// This compact ECC200 encoder deliberately uses Data Matrix ASCII encoding only.
// It still accepts arbitrary UTF-8 bytes (bytes >= 128 use Upper Shift), but does
// not emit ECI, Macro, structured append, or GS1 FNC1 control codewords.
std::vector<uint8_t> encodeDataMatrixAscii(const std::string& text) {
    std::vector<uint8_t> out;
    out.reserve(text.size() * 2U + 1U);
    for (std::size_t i = 0; i < text.size();) {
        const uint8_t c = static_cast<uint8_t>(text[i]);
        if (i + 1U < text.size() && c >= '0' && c <= '9' &&
            static_cast<uint8_t>(text[i + 1U]) >= '0' &&
            static_cast<uint8_t>(text[i + 1U]) <= '9') {
            const uint8_t pair = static_cast<uint8_t>((c - '0') * 10U +
                                                       (static_cast<uint8_t>(text[i + 1U]) - '0'));
            out.push_back(static_cast<uint8_t>(130U + pair));
            i += 2U;
        } else {
            if (c > 127U) {
                out.push_back(235U);  // Upper Shift
                out.push_back(static_cast<uint8_t>((c & 127U) + 1U));
            } else {
                out.push_back(static_cast<uint8_t>(c + 1U));
            }
            ++i;
        }
    }
    return out;
}

struct DataMatrixSize {
    int dataWidth = 0;
    int dataHeight = 0;
    int regionColumns = 1;
    int regionRows = 1;
    int blockCount = 1;
    int totalCodewords = 0;
    int errorCodewords = 0;
};

bool chooseDataMatrixSize(std::size_t encodedLength, bool rectangular, DataMatrixSize& out) {
    int w = 0;
    int h = 0;
    int l = 0;
    int s = 0;
    int nc = 1;
    int nr = 1;
    int blocks = 1;

    if (rectangular && encodedLength < 50U) {
        // Width without finder pattern, error codewords. Heights follow the
        // compact progression used by the reference implementation.
        static constexpr int kRect[] = {16, 7, 28, 11, 24, 14, 32, 18, 32, 24, 44, 28};
        bool found = false;
        for (int j = 0; j < static_cast<int>(sizeof(kRect) / sizeof(kRect[0])); j += 2) {
            w = kRect[j];
            h = 6 + (j & 12);
            l = w * h / 8;
            s = kRect[j + 1];
            if (l - s >= static_cast<int>(encodedLength)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
        if (w > 25) nc = 2;
    } else {
        static constexpr int kSquareEcc[] = {
            5, 7, 10, 12, 14, 18, 20, 24, 28, 36, 42, 48, 56, 68, 84,
            112, 144, 192, 224, 272, 336, 408, 496, 620
        };
        w = h = 6;
        int increment = 2;
        bool found = false;
        for (int ecc : kSquareEcc) {
            if (w > 11 * increment) increment = (4 + increment) & 12;
            w = h = h + increment;
            l = w * h / 8;
            s = ecc;
            if (l - s >= static_cast<int>(encodedLength)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
        if (w > 27) nr = nc = 2 * (w / 54) + 2;
        if (l > 255) blocks = 2 * (l >> 9) + 2;
    }

    out.dataWidth = w;
    out.dataHeight = h;
    out.regionColumns = nc;
    out.regionRows = nr;
    out.blockCount = blocks;
    out.totalCodewords = l;
    out.errorCodewords = s;
    return true;
}

void appendDataMatrixErrorCorrection(std::vector<uint8_t>& codewords,
                                     int dataCodewords,
                                     int errorCodewords,
                                     int blockCount) {
    std::array<int, 256> log{};
    std::array<int, 255> exp{};
    int value = 1;
    for (int i = 0; i < 255; ++i) {
        exp[static_cast<std::size_t>(i)] = value;
        log[static_cast<std::size_t>(value)] = i;
        value += value;
        if (value > 255) value ^= 301;  // x^8 + x^5 + x^3 + x^2 + 1
    }

    const int eccPerBlock = errorCodewords / blockCount;
    std::vector<int> generator(static_cast<std::size_t>(eccPerBlock + 1), 0);
    for (int i = 1; i <= eccPerBlock; ++i) {
        generator[static_cast<std::size_t>(eccPerBlock - i)] = 1;
        for (int j = eccPerBlock - i; j < eccPerBlock; ++j) {
            const int left = generator[static_cast<std::size_t>(j + 1)];
            generator[static_cast<std::size_t>(j)] =
                left ^ exp[static_cast<std::size_t>((log[static_cast<std::size_t>(generator[static_cast<std::size_t>(j)])] + i) % 255)];
        }
    }

    for (int block = 0; block < blockCount; ++block) {
        std::vector<int> correction(static_cast<std::size_t>(eccPerBlock + 1), 0);
        for (int i = block; i < dataCodewords; i += blockCount) {
            const int x = correction[0] ^ codewords[static_cast<std::size_t>(i)];
            for (int j = 0; j < eccPerBlock; ++j) {
                const int product = x == 0
                    ? 0
                    : exp[static_cast<std::size_t>((log[static_cast<std::size_t>(generator[static_cast<std::size_t>(j)])] +
                                                    log[static_cast<std::size_t>(x)]) % 255)];
                correction[static_cast<std::size_t>(j)] = correction[static_cast<std::size_t>(j + 1)] ^ product;
            }
        }
        for (int i = 0; i < eccPerBlock; ++i) {
            codewords[static_cast<std::size_t>(dataCodewords + block + i * blockCount)] =
                static_cast<uint8_t>(correction[static_cast<std::size_t>(i)]);
        }
    }
}

void setDataMatrixModule(BitMatrix& matrix,
                         int dataWidth,
                         int dataHeight,
                         int regionDataWidth,
                         int regionDataHeight,
                         int column,
                         int row) {
    if (column < 0) {
        column += dataWidth;
        row += 4 - ((dataWidth + 4) & 7);
    }
    if (row < 0) {
        row += dataHeight;
        column += 4 - ((dataHeight + 4) & 7);
    }
    const int x = column + (column / regionDataWidth) * 2 + 1;
    const int y = row + (row / regionDataHeight) * 2 + 1;
    if (x >= 0 && y >= 0 && x < matrix.width() && y < matrix.height()) {
        matrix.set(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
    }
}

}  // namespace

BarcodeResult encodeDataMatrix(const BarcodeSpec& spec) {
    if (spec.data.empty()) return matrixFailure("Data Matrix payload must not be empty");

    std::vector<uint8_t> codewords = encodeDataMatrixAscii(spec.data);
    DataMatrixSize size;
    if (!chooseDataMatrixSize(codewords.size(), spec.dataMatrixRectangular, size)) {
        return matrixFailure("Data Matrix payload is too large for an ECC200 symbol");
    }

    const int dataCapacity = size.totalCodewords - size.errorCodewords;
    if (static_cast<int>(codewords.size()) < dataCapacity) codewords.push_back(129U);
    while (static_cast<int>(codewords.size()) < dataCapacity) {
        const int position = static_cast<int>(codewords.size()) + 1;  // 1-based codeword position
        codewords.push_back(static_cast<uint8_t>(((149 * position) % 253 + 130) % 254));
    }
    codewords.resize(static_cast<std::size_t>(size.totalCodewords), 0U);
    appendDataMatrixErrorCorrection(codewords,
                                    dataCapacity,
                                    size.errorCodewords,
                                    size.blockCount);

    const int regionDataWidth = size.dataWidth / size.regionColumns;
    const int regionDataHeight = size.dataHeight / size.regionRows;
    const int symbolWidth = size.dataWidth + 2 * size.regionColumns;
    const int symbolHeight = size.dataHeight + 2 * size.regionRows;
    BitMatrix matrix(static_cast<uint16_t>(symbolWidth), static_cast<uint16_t>(symbolHeight));

    // Finder and timing borders around every data region.
    for (int y = 0; y < symbolHeight; ++y) {
        for (int x = 0; x < symbolWidth; ++x) {
            const bool black =
                x % (regionDataWidth + 2) == 0 ||
                (y + 1) % (regionDataHeight + 2) == 0 ||
                ((x + 1) % (regionDataWidth + 2) == 0 && (y & 1) != 0) ||
                (y % (regionDataHeight + 2) == 0 && (x & 1) == 0);
            if (black) matrix.set(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
        }
    }

    // Utah and corner placement. Codewords are consumed least-significant bit
    // first, matching the compact reference algorithm.
    int step = 2;
    int column = -2;
    int row = 6;
    int index = 0;
    while (index < size.totalCodewords) {
        row -= step;
        column += step;
        std::array<int, 16> offsets{};

        if (row == size.dataHeight - 3 && column == -1) {
            offsets = {size.dataWidth, 6 - size.dataHeight,
                       size.dataWidth, 5 - size.dataHeight,
                       size.dataWidth, 4 - size.dataHeight,
                       size.dataWidth, 3 - size.dataHeight,
                       size.dataWidth - 1, 3 - size.dataHeight,
                       3, 2, 2, 2, 1, 2};
        } else if (row == size.dataHeight + 1 && column == 1 &&
                   (size.dataWidth & 7) == 0 && (size.dataHeight & 7) == 6) {
            offsets = {size.dataWidth - 2, -size.dataHeight,
                       size.dataWidth - 3, -size.dataHeight,
                       size.dataWidth - 4, -size.dataHeight,
                       size.dataWidth - 2, -1 - size.dataHeight,
                       size.dataWidth - 3, -1 - size.dataHeight,
                       size.dataWidth - 4, -1 - size.dataHeight,
                       size.dataWidth - 2, -2,
                       -1, -2};
        } else {
            if (row == 0 && column == size.dataWidth - 2 && (size.dataWidth & 3) != 0) continue;
            if (row < 0 || column >= size.dataWidth || row >= size.dataHeight || column < 0) {
                step = -step;
                row += 2 + step / 2;
                column += 2 - step / 2;
                while (row < 0 || column >= size.dataWidth || row >= size.dataHeight || column < 0) {
                    row -= step;
                    column += step;
                }
            }
            if (row == size.dataHeight - 2 && column == 0 && (size.dataWidth & 3) != 0) {
                offsets = {size.dataWidth - 1, 3 - size.dataHeight,
                           size.dataWidth - 1, 2 - size.dataHeight,
                           size.dataWidth - 2, 2 - size.dataHeight,
                           size.dataWidth - 3, 2 - size.dataHeight,
                           size.dataWidth - 4, 2 - size.dataHeight,
                           0, 1, 0, 0, 0, -1};
            } else if (row == size.dataHeight - 2 && column == 0 && (size.dataWidth & 7) == 4) {
                offsets = {size.dataWidth - 1, 5 - size.dataHeight,
                           size.dataWidth - 1, 4 - size.dataHeight,
                           size.dataWidth - 1, 3 - size.dataHeight,
                           size.dataWidth - 1, 2 - size.dataHeight,
                           size.dataWidth - 2, 2 - size.dataHeight,
                           0, 1, 0, 0, 0, -1};
            } else if (row == 1 && column == size.dataWidth - 1 &&
                       (size.dataWidth & 7) == 0 && (size.dataHeight & 7) == 6) {
                continue;
            } else {
                offsets = {0, 0, -1, 0, -2, 0, 0, -1,
                           -1, -1, -2, -1, -1, -2, -2, -2};
            }
        }

        uint8_t codeword = codewords[static_cast<std::size_t>(index++)];
        for (int bit = 0; bit < 8; ++bit) {
            if ((codeword & 1U) != 0U) {
                setDataMatrixModule(matrix,
                                    size.dataWidth,
                                    size.dataHeight,
                                    regionDataWidth,
                                    regionDataHeight,
                                    column + offsets[static_cast<std::size_t>(bit * 2)],
                                    row + offsets[static_cast<std::size_t>(bit * 2 + 1)]);
            }
            codeword >>= 1U;
        }
    }

    for (int i = size.dataWidth & -4; i < size.dataWidth; ++i) {
        const int p = i + 1;
        if (p >= 0 && p < matrix.width() && p < matrix.height()) {
            matrix.set(static_cast<uint16_t>(p), static_cast<uint16_t>(p));
        }
    }

    return matrixResult(std::move(matrix), spec.data, 1);
}

}  // namespace espbarcode
