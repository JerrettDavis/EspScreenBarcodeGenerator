#pragma once

#include <cstdint>
#include <vector>

#include "ConnectivityTypes.h"
#include "Envelope.h"  // reuses CodecError

namespace esplink {

struct HopFrameHeader {
    uint8_t major = 2;
    uint8_t minor = 0;
    FrameType frameType = FrameType::Data;
    uint8_t flags = 0;
    TrafficClass trafficClass = TrafficClass::Control;
    CarrierProfileId profileId = CarrierProfileId::Unspecified;
    uint16_t routeId = 0;
    uint32_t linkSessionId = 0;
    uint32_t linkMessageId = 0;
    uint32_t linkCorrelationId = 0;
    uint16_t fragmentIndex = 0;
    uint16_t fragmentCount = 1;
};

inline constexpr std::size_t kHopFrameHeaderSize = 32;
inline constexpr std::size_t kHopFrameOverhead = 36;  // header(32) + crc32 trailer(4)

// Encodes header + payload + trailing CRC-32 (computed over header+payload) into `out`.
bool encodeHopFrame(const HopFrameHeader& header, const uint8_t* payload, std::size_t payloadLength,
                    std::vector<uint8_t>& out, CodecError& error);

// Decodes and CRC-validates a single raw hop frame. `payload` receives just the fragment bytes.
bool decodeHopFrame(const uint8_t* bytes, std::size_t length, HopFrameHeader& header,
                    std::vector<uint8_t>& payload, CodecError& error);

}  // namespace esplink
