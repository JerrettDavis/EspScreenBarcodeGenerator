#include "HopFrame.h"

#include "Crc32.h"

namespace esplink {

namespace {
void putU16(std::vector<uint8_t>& out, uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
uint16_t getU16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

bool encodeHopFrame(const HopFrameHeader& header, const uint8_t* payload, std::size_t payloadLength,
                    std::vector<uint8_t>& out, CodecError& error) {
    if (payloadLength > 0xFFFFULL) { error = CodecError::PayloadTooLarge; return false; }
    out.clear();
    out.reserve(kHopFrameOverhead + payloadLength);
    out.push_back('E');
    out.push_back('L');
    out.push_back(header.major);
    out.push_back(header.minor);
    out.push_back(static_cast<uint8_t>(header.frameType));
    out.push_back(header.flags);
    out.push_back(static_cast<uint8_t>(header.trafficClass));
    out.push_back(static_cast<uint8_t>(header.profileId));
    putU16(out, header.routeId);
    putU16(out, static_cast<uint16_t>(kHopFrameHeaderSize));
    putU32(out, header.linkSessionId);
    putU32(out, header.linkMessageId);
    putU32(out, header.linkCorrelationId);
    putU16(out, header.fragmentIndex);
    putU16(out, header.fragmentCount);
    putU16(out, static_cast<uint16_t>(payloadLength));
    putU16(out, 0);  // reserved
    out.insert(out.end(), payload, payload + payloadLength);

    const uint32_t crc = crc32(out.data(), out.size());
    putU32(out, crc);
    error = CodecError::None;
    return true;
}

bool decodeHopFrame(const uint8_t* bytes, std::size_t length, HopFrameHeader& header,
                    std::vector<uint8_t>& payload, CodecError& error) {
    if (length < kHopFrameOverhead) { error = CodecError::TooShort; return false; }
    if (bytes[0] != 'E' || bytes[1] != 'L') { error = CodecError::BadMagic; return false; }
    header.major = bytes[2];
    header.minor = bytes[3];
    if (header.major != 2) { error = CodecError::UnsupportedMajorVersion; return false; }
    header.frameType = static_cast<FrameType>(bytes[4]);
    header.flags = bytes[5];
    header.trafficClass = static_cast<TrafficClass>(bytes[6]);
    header.profileId = static_cast<CarrierProfileId>(bytes[7]);
    header.routeId = getU16(bytes + 8);
    const uint16_t headerLength = getU16(bytes + 10);
    if (headerLength != kHopFrameHeaderSize) { error = CodecError::BadHeaderLength; return false; }
    header.linkSessionId = getU32(bytes + 12);
    header.linkMessageId = getU32(bytes + 16);
    header.linkCorrelationId = getU32(bytes + 20);
    header.fragmentIndex = getU16(bytes + 24);
    header.fragmentCount = getU16(bytes + 26);
    const uint16_t payloadLength = getU16(bytes + 28);
    const uint16_t reserved = getU16(bytes + 30);
    if (reserved != 0) { error = CodecError::ReservedFieldNonZero; return false; }
    if (header.fragmentCount == 0 || header.fragmentIndex >= header.fragmentCount) {
        error = CodecError::InvalidFragmentIndex;
        return false;
    }

    const std::size_t rawLength = kHopFrameHeaderSize + std::size_t(payloadLength) + 4U;
    if (length < rawLength) { error = CodecError::TooShort; return false; }

    const uint32_t expectedCrc = getU32(bytes + kHopFrameHeaderSize + payloadLength);
    const uint32_t actualCrc = crc32(bytes, kHopFrameHeaderSize + payloadLength);
    if (expectedCrc != actualCrc) { error = CodecError::CrcMismatch; return false; }

    payload.assign(bytes + kHopFrameHeaderSize, bytes + kHopFrameHeaderSize + payloadLength);
    error = CodecError::None;
    return true;
}

}  // namespace esplink
