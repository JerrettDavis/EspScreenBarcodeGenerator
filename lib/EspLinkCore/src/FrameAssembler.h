#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "HopFrame.h"

namespace esplink {

enum class AssemblyOutcome : uint8_t { Incomplete, Complete, DuplicateIgnored, Conflict };

class FrameAssembler {
public:
    explicit FrameAssembler(std::size_t maxConcurrentMessages = 2);

    // Feeds one already CRC-validated fragment. On AssemblyOutcome::Complete, `assembled`
    // holds the full reassembled Layer 3 bytes (envelope + body) in fragment order.
    AssemblyOutcome addFragment(const HopFrameHeader& header, const std::vector<uint8_t>& payload,
                                std::vector<uint8_t>& assembled);

private:
    struct Key { uint32_t linkSessionId; uint32_t linkMessageId; uint16_t routeId; };
    struct Partial {
        Key key{};
        uint16_t fragmentCount = 0;
        uint16_t receivedCount = 0;
        std::vector<std::optional<std::vector<uint8_t>>> fragments;
    };

    static bool sameKey(const Key& a, const Key& b);

    std::size_t maxConcurrentMessages_;
    std::vector<Partial> partial_;
};

}  // namespace esplink
