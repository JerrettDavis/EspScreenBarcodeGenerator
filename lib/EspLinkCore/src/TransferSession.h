#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EspBarcodeCore.h"

namespace esplink {

struct TransferState {
    bool active = false;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 4;
    espbarcode::Rotation rotation = espbarcode::Rotation::Auto;
    bool invert = false;
    bool display = true;
    std::string label;
    std::vector<uint8_t> bytes;
    std::size_t nextOffset = 0;

    void reset() { *this = TransferState{}; }
};

class TransferSession {
public:
    TransferState& upload() { return upload_; }
    const TransferState& upload() const { return upload_; }

private:
    TransferState upload_;
};

}  // namespace esplink
