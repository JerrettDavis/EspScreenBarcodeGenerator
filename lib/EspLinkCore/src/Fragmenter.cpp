#include "Fragmenter.h"

#include <algorithm>

namespace esplink {

bool fragmentIntoHopFrames(const uint8_t* message, std::size_t messageLength, const HopFrameHeader& templateHeader,
                            std::size_t maxFrameBytes, std::vector<std::vector<uint8_t>>& outFrames,
                            CodecError& error) {
    outFrames.clear();
    if (message == nullptr || messageLength == 0) {
        error = CodecError::TooShort;
        return false;
    }
    if (maxFrameBytes <= kHopFrameOverhead) {
        error = CodecError::PayloadTooLarge;
        return false;
    }

    const std::size_t maxPayload = maxFrameBytes - kHopFrameOverhead;
    const std::size_t fragmentCount = (messageLength + maxPayload - 1) / maxPayload;
    if (fragmentCount == 0 || fragmentCount > 0xFFFF) {
        error = CodecError::PayloadTooLarge;
        return false;
    }

    outFrames.reserve(fragmentCount);
    for (std::size_t i = 0; i < fragmentCount; ++i) {
        const std::size_t offset = i * maxPayload;
        const std::size_t length = std::min(maxPayload, messageLength - offset);

        HopFrameHeader header = templateHeader;
        header.fragmentIndex = static_cast<uint16_t>(i);
        header.fragmentCount = static_cast<uint16_t>(fragmentCount);

        std::vector<uint8_t> frame;
        CodecError frameError;
        if (!encodeHopFrame(header, message + offset, length, frame, frameError)) {
            outFrames.clear();
            error = frameError;
            return false;
        }
        outFrames.push_back(std::move(frame));
    }

    error = CodecError::None;
    return true;
}

}  // namespace esplink
