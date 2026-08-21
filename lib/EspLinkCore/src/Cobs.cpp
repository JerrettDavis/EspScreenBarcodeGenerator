#include "Cobs.h"

namespace esplink {

std::vector<uint8_t> cobsEncode(const uint8_t* data, std::size_t length) {
    std::vector<uint8_t> out;
    out.reserve(length + length / 254 + 2);
    std::size_t codeIndex = 0;
    out.push_back(0);  // placeholder for the first code byte, backpatched below
    uint8_t code = 1;
    for (std::size_t i = 0; i < length; ++i) {
        if (data[i] == 0x00) {
            out[codeIndex] = code;
            codeIndex = out.size();
            out.push_back(0);  // placeholder for the next code byte
            code = 1;
        } else {
            out.push_back(data[i]);
            ++code;
            if (code == 0xFF) {
                out[codeIndex] = code;
                codeIndex = out.size();
                out.push_back(0);  // placeholder
                code = 1;
            }
        }
    }
    out[codeIndex] = code;  // finalize the last (possibly empty) group unconditionally
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
