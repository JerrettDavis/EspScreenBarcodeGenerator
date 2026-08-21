#include "LengthPrefixFraming.h"

namespace esplink {

std::vector<uint8_t> encodeLengthPrefixedFrame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.reserve(payload.size() + 4);
    const uint32_t length = static_cast<uint32_t>(payload.size());
    out.push_back(static_cast<uint8_t>(length & 0xFF));
    out.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((length >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((length >> 24) & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

LengthPrefixFrameParser::LengthPrefixFrameParser(std::size_t maxFrameBytes) : maxFrameBytes_(maxFrameBytes) {}

bool LengthPrefixFrameParser::feed(const uint8_t* data, std::size_t length,
                                    std::vector<std::vector<uint8_t>>& outFrames) {
    if (error_) return false;
    if (data != nullptr && length > 0) {
        buffer_.insert(buffer_.end(), data, data + length);
    }

    std::size_t consumed = 0;
    for (;;) {
        if (!haveLength_) {
            if (buffer_.size() - consumed < 4) break;
            const uint32_t declared = static_cast<uint32_t>(buffer_[consumed]) |
                                       (static_cast<uint32_t>(buffer_[consumed + 1]) << 8) |
                                       (static_cast<uint32_t>(buffer_[consumed + 2]) << 16) |
                                       (static_cast<uint32_t>(buffer_[consumed + 3]) << 24);
            if (declared == 0 || declared > maxFrameBytes_) {
                error_ = true;
                buffer_.clear();
                return false;
            }
            expectedLength_ = declared;
            haveLength_ = true;
            consumed += 4;
        }

        if (buffer_.size() - consumed < expectedLength_) break;
        outFrames.emplace_back(buffer_.begin() + static_cast<std::ptrdiff_t>(consumed),
                                buffer_.begin() + static_cast<std::ptrdiff_t>(consumed + expectedLength_));
        consumed += expectedLength_;
        haveLength_ = false;
    }

    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
    return true;
}

void LengthPrefixFrameParser::reset() {
    buffer_.clear();
    expectedLength_ = 0;
    haveLength_ = false;
    error_ = false;
}

}  // namespace esplink
