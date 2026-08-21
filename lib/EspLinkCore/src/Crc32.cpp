#include "Crc32.h"

namespace esplink {

uint32_t crc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

}  // namespace esplink
