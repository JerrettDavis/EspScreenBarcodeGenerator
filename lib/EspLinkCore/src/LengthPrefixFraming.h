#pragma once

#include <cstdint>
#include <vector>

namespace esplink {

// "Wi-Fi Direct TCP" carrier framing (docs/PROTOCOL_V2.md §8.6): `uint32_le frameLength +
// raw EspLink hop frame`. Encodes one already-fragmented hop frame for a length-prefixed
// stream carrier (`CarrierProfileId::TcpStandard`).
std::vector<uint8_t> encodeLengthPrefixedFrame(const std::vector<uint8_t>& payload);

// Incremental parser for the same framing: feed it raw bytes as they arrive from a TCP
// stream and it yields complete frames as soon as each is fully buffered. Kept free of any
// socket/Arduino dependency so it can run under the native test harness.
//
// Per docs/PROTOCOL_V2.md §8.6, a zero or oversized declared length is rejected rather than
// silently truncated or resynchronized: once `feed()` returns false the parser is
// permanently in an error state (`hasError()`) and the caller must close/reset the
// underlying connection — call `reset()` only after establishing a fresh connection.
class LengthPrefixFrameParser {
public:
    explicit LengthPrefixFrameParser(std::size_t maxFrameBytes);

    // Appends `length` bytes starting at `data` and moves any newly-completed frames into
    // `outFrames`, in order. Returns false the moment a declared frame length is zero or
    // exceeds `maxFrameBytes`; `outFrames` still holds any frames completed before the bad
    // length was read.
    bool feed(const uint8_t* data, std::size_t length, std::vector<std::vector<uint8_t>>& outFrames);

    bool hasError() const { return error_; }
    void reset();

private:
    std::size_t maxFrameBytes_;
    std::vector<uint8_t> buffer_;
    uint32_t expectedLength_ = 0;
    bool haveLength_ = false;
    bool error_ = false;
};

}  // namespace esplink
