#pragma once

#include <cstdint>
#include <vector>

#include "HopFrame.h"

namespace esplink {

// Splits an already-encoded Layer 3 message (envelope + body) into a sequence of raw,
// CRC-trailed hop frames, each no larger than `maxFrameBytes`. Every frame shares
// `templateHeader`'s session/route/traffic-class/profile fields; only `fragmentIndex`
// and `fragmentCount` are overwritten per fragment. Frames are returned in order and are
// ready to hand directly to a carrier's send primitive (no further framing needed for a
// datagram carrier such as ESP-NOW).
//
// Fails if `message` is empty, if `maxFrameBytes` cannot hold even one payload byte after
// `kHopFrameOverhead`, or if the resulting fragment count would exceed `fragmentCount`'s
// uint16_t range.
bool fragmentIntoHopFrames(const uint8_t* message, std::size_t messageLength, const HopFrameHeader& templateHeader,
                            std::size_t maxFrameBytes, std::vector<std::vector<uint8_t>>& outFrames,
                            CodecError& error);

}  // namespace esplink
