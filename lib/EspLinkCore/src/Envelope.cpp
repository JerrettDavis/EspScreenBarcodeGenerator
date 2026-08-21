#include "Envelope.h"

#include <cstring>

namespace esplink {

namespace {
void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
void putU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t getU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
}  // namespace

bool encodeEnvelope(const MessageEnvelope& envelope, const std::vector<uint8_t>& body,
                    std::vector<uint8_t>& out, CodecError& error) {
    if (body.size() > 0xFFFFFFFFULL) { error = CodecError::PayloadTooLarge; return false; }
    out.clear();
    out.reserve(kEnvelopeHeaderSize + body.size());
    out.push_back('E');
    out.push_back('M');
    out.push_back(envelope.major);
    out.push_back(envelope.minor);
    out.push_back(static_cast<uint8_t>(envelope.kind));
    out.push_back(envelope.flags);
    out.push_back(static_cast<uint8_t>(envelope.serviceId));
    out.push_back(static_cast<uint8_t>(envelope.codecId));
    putU32(out, envelope.controlSessionId);
    putU32(out, static_cast<uint32_t>(body.size()));
    putU64(out, envelope.operationId);
    putU64(out, envelope.correlationId);
    out.insert(out.end(), body.begin(), body.end());
    error = CodecError::None;
    return true;
}

bool decodeEnvelope(const uint8_t* bytes, std::size_t length, MessageEnvelope& envelope,
                    std::vector<uint8_t>& body, CodecError& error) {
    if (length < kEnvelopeHeaderSize) { error = CodecError::TooShort; return false; }
    if (bytes[0] != 'E' || bytes[1] != 'M') { error = CodecError::BadMagic; return false; }
    envelope.major = bytes[2];
    envelope.minor = bytes[3];
    if (envelope.major != 2) { error = CodecError::UnsupportedMajorVersion; return false; }
    envelope.kind = static_cast<MessageKind>(bytes[4]);
    envelope.flags = bytes[5];
    envelope.serviceId = static_cast<ServiceId>(bytes[6]);
    envelope.codecId = static_cast<CodecId>(bytes[7]);
    envelope.controlSessionId = getU32(bytes + 8);
    envelope.bodyLength = getU32(bytes + 12);
    envelope.operationId = getU64(bytes + 16);
    envelope.correlationId = getU64(bytes + 24);

    if (length - kEnvelopeHeaderSize < envelope.bodyLength) { error = CodecError::BodyLengthMismatch; return false; }
    body.assign(bytes + kEnvelopeHeaderSize, bytes + kEnvelopeHeaderSize + envelope.bodyLength);
    error = CodecError::None;
    return true;
}

}  // namespace esplink
