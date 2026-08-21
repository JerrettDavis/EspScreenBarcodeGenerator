#pragma once

#include <cstddef>
#include <cstdint>

namespace esplink {
uint32_t crc32(const uint8_t* data, std::size_t length);
}
