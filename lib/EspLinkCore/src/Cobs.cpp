#include "Cobs.h"

namespace esplink {

std::vector<uint8_t> cobsEncode(const uint8_t* data, std::size_t length) {
    std::vector<uint8_t> out;
    out.reserve(length + length / 254 + 2);
    std::size_t read = 0;
    while (true) {
        std::size_t blockStart = read;
        std::size_t blockLen = 0;
        while (read < length && data[read] != 0x00 && blockLen < 254) { ++read; ++blockLen; }
        const bool hitZero = read < length && data[read] == 0x00;
        out.push_back(static_cast<uint8_t>(blockLen + 1));
        out.insert(out.end(), data + blockStart, data + blockStart + blockLen);
        if (hitZero) {
            ++read;
            if (read >= length) break;
        } else if (read >= length) {
            break;
        }
        // else: blockLen hit the 254 cap mid-run; loop continues with code 255 on the next block.
    }
    return out;
}

bool cobsDecode(const uint8_t* data, std::size_t length, std::vector<uint8_t>& out) {
    out.clear();
    std::size_t read = 0;
    while (read < length) {
        const uint8_t code = data[read];
        if (code == 0) return false;  // a literal zero byte is never valid inside a COBS block.
        ++read;
        const std::size_t blockLen = std::size_t(code) - 1;
        if (read + blockLen > length) return false;
        out.insert(out.end(), data + read, data + read + blockLen);
        read += blockLen;
        if (code != 255 && read < length) out.push_back(0x00);
    }
    return true;
}

}  // namespace esplink
