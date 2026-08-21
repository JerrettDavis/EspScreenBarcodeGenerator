#pragma once

#include <cstdint>
#include <vector>

#include "ConnectivityTypes.h"

namespace esplink {

struct MessageEnvelope {
    uint8_t major = 2;
    uint8_t minor = 0;
    MessageKind kind = MessageKind::Command;
    uint8_t flags = 0;
    ServiceId serviceId = ServiceId::System;
    CodecId codecId = CodecId::Json;
    uint32_t controlSessionId = 0;
    uint32_t bodyLength = 0;
    uint64_t operationId = 0;
    uint64_t correlationId = 0;
};

inline constexpr std::size_t kEnvelopeHeaderSize = 32;

enum class CodecError : uint8_t {
    None, TooShort, BadMagic, UnsupportedMajorVersion, BodyLengthMismatch,
    CrcMismatch, PayloadTooLarge, ReservedFieldNonZero, BadHeaderLength,
    InvalidFragmentIndex, InvalidRoute,
};

// Writes the 32-byte envelope header followed by `body` into `out` (replacing its contents).
// Fails only if `body.size()` does not fit in a uint32_t.
bool encodeEnvelope(const MessageEnvelope& envelope, const std::vector<uint8_t>& body,
                    std::vector<uint8_t>& out, CodecError& error);

// Decodes a header from the front of `bytes` and slices the declared body out of the
// remainder. `bytes` must contain at least `envelope.bodyLength` bytes after the header;
// trailing bytes beyond the body are ignored (callers pass exactly one Layer 3 message).
bool decodeEnvelope(const uint8_t* bytes, std::size_t length, MessageEnvelope& envelope,
                    std::vector<uint8_t>& body, CodecError& error);

}  // namespace esplink
