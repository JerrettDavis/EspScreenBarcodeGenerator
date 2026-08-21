#pragma once

#include <cstdint>
#include <vector>

namespace esplink {

std::vector<uint8_t> cobsEncode(const uint8_t* data, std::size_t length);
bool cobsDecode(const uint8_t* data, std::size_t length, std::vector<uint8_t>& out);

}  // namespace esplink
