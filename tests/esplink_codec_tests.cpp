#include "Cobs.h"
#include "Crc32.h"
#include "Envelope.h"
#include "FrameAssembler.h"
#include "HopFrame.h"

#include <iomanip>
#include <iostream>
#include <sstream>

using namespace esplink;

namespace {
int failures = 0;
void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
}
}  // namespace
#define CHECK(expr) check((expr), #expr, __LINE__)

namespace {

std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

// Vector A: Layer 3 envelope wrapping {"schema":"esbg.control/2.0","name":"system.ping","body":{}}
const std::string kVectorAHex =
    "454d020000000000000000003c000000010000000000000000000000000000007b22736368656d61223a22"
    "657362672e636f6e74726f6c2f322e30222c226e616d65223a2273797374656d2e70696e67222c22626f6479"
    "223a7b7d7d";

void test_envelope_encode_matches_vector_a() {
    MessageEnvelope env;
    env.major = 2; env.minor = 0; env.kind = MessageKind::Command; env.flags = 0;
    env.serviceId = ServiceId::System; env.codecId = CodecId::Json;
    env.controlSessionId = 0; env.operationId = 1; env.correlationId = 0;
    const std::vector<uint8_t> body = fromHex(
        "7b22736368656d61223a22657362672e636f6e74726f6c2f322e30222c226e616d65223a2273797374656d"
        "2e70696e67222c22626f6479223a7b7d7d");

    std::vector<uint8_t> out;
    CodecError error;
    CHECK(encodeEnvelope(env, body, out, error));
    CHECK(error == CodecError::None);
    CHECK(out == fromHex(kVectorAHex));
}

void test_envelope_decode_round_trips_vector_a() {
    const auto bytes = fromHex(kVectorAHex);
    MessageEnvelope env;
    std::vector<uint8_t> body;
    CodecError error;
    CHECK(decodeEnvelope(bytes.data(), bytes.size(), env, body, error));
    CHECK(error == CodecError::None);
    CHECK(env.major == 2);
    CHECK(env.kind == MessageKind::Command);
    CHECK(env.serviceId == ServiceId::System);
    CHECK(env.operationId == 1);
    CHECK(env.correlationId == 0);
    CHECK(body.size() == 60);
}

void test_envelope_rejects_truncated_input() {
    const auto bytes = fromHex(kVectorAHex.substr(0, 20));  // shorter than the 32-byte header
    MessageEnvelope env;
    std::vector<uint8_t> body;
    CodecError error;
    CHECK(!decodeEnvelope(bytes.data(), bytes.size(), env, body, error));
    CHECK(error == CodecError::TooShort);
}

// Vector B: single hop frame (profile stream-standard, linkSessionId=1001, linkMessageId=1)
// wrapping Vector A's message. Bytes verified against a reference implementation.
const std::string kVectorBHex =
    "454c02000001000400002000e90300000100000000000000000001005c000000454d0200000000000000000"
    "03c000000010000000000000000000000000000007b22736368656d61223a22657362672e636f6e74726f6c2f"
    "322e30222c226e616d65223a2273797374656d2e70696e67222c22626f6479223a7b7d7dee2c40c3";

void test_hop_frame_encode_matches_vector_b() {
    HopFrameHeader header;
    header.frameType = FrameType::Data;
    header.flags = 0x01;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::StreamStandard;
    header.routeId = 0;
    header.linkSessionId = 1001;
    header.linkMessageId = 1;
    header.linkCorrelationId = 0;
    header.fragmentIndex = 0;
    header.fragmentCount = 1;
    const auto payload = fromHex(kVectorAHex);

    std::vector<uint8_t> out;
    CodecError error;
    CHECK(encodeHopFrame(header, payload.data(), payload.size(), out, error));
    CHECK(error == CodecError::None);
    CHECK(out == fromHex(kVectorBHex));
    CHECK(out.size() == 36 + payload.size());
}

void test_hop_frame_decode_validates_crc_and_recovers_payload() {
    const auto raw = fromHex(kVectorBHex);
    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError error;
    CHECK(decodeHopFrame(raw.data(), raw.size(), header, payload, error));
    CHECK(error == CodecError::None);
    CHECK(header.profileId == CarrierProfileId::StreamStandard);
    CHECK(header.fragmentIndex == 0);
    CHECK(header.fragmentCount == 1);
    CHECK(payload == fromHex(kVectorAHex));
}

void test_hop_frame_decode_rejects_corrupted_payload() {
    auto raw = fromHex(kVectorBHex);
    raw[raw.size() - 5] ^= 0xFF;  // flip a payload byte without fixing the trailing CRC
    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError error;
    CHECK(!decodeHopFrame(raw.data(), raw.size(), header, payload, error));
    CHECK(error == CodecError::CrcMismatch);
}

void test_cobs_round_trips_a_frame_containing_zero_bytes() {
    const auto raw = fromHex(kVectorBHex);
    const auto encoded = cobsEncode(raw.data(), raw.size());
    for (uint8_t b : encoded) CHECK(b != 0x00);  // COBS output never contains a literal zero.
    std::vector<uint8_t> decoded;
    CHECK(cobsDecode(encoded.data(), encoded.size(), decoded));
    CHECK(decoded == raw);
}

void test_cobs_rejects_a_literal_zero_inside_the_block() {
    std::vector<uint8_t> corrupt = {0x03, 0x01, 0x00, 0x02};
    std::vector<uint8_t> decoded;
    CHECK(!cobsDecode(corrupt.data(), corrupt.size(), decoded));
}

void test_cobs_round_trips_data_ending_in_zero() {
    const std::vector<uint8_t> raw = {0x41, 0x00};
    const auto encoded = cobsEncode(raw.data(), raw.size());
    for (uint8_t b : encoded) CHECK(b != 0x00);
    std::vector<uint8_t> decoded;
    CHECK(cobsDecode(encoded.data(), encoded.size(), decoded));
    CHECK(decoded == raw);
}

void test_cobs_round_trips_a_lone_zero_byte() {
    const std::vector<uint8_t> raw = {0x00};
    const auto encoded = cobsEncode(raw.data(), raw.size());
    for (uint8_t b : encoded) CHECK(b != 0x00);
    std::vector<uint8_t> decoded;
    CHECK(cobsDecode(encoded.data(), encoded.size(), decoded));
    CHECK(decoded == raw);
}

void test_cobs_round_trips_254_byte_run_followed_by_zero() {
    std::vector<uint8_t> raw(254, 0x41);
    raw.push_back(0x00);
    raw.push_back(0x42);
    const auto encoded = cobsEncode(raw.data(), raw.size());
    for (uint8_t b : encoded) CHECK(b != 0x00);
    std::vector<uint8_t> decoded;
    CHECK(cobsDecode(encoded.data(), encoded.size(), decoded));
    CHECK(decoded == raw);
}

// Vector D: barcode.generate split across two fragments over espnow-v1 (max payload 214),
// linkSessionId=2002, linkMessageId=5.
void test_frame_assembler_reassembles_two_fragments_in_order() {
    // Layer 3 message: envelope(serviceId=Barcode, operationId=7) + a long JSON body, split
    // at byte 214 (espnow-v1's fragment payload ceiling).
    MessageEnvelope env;
    env.serviceId = ServiceId::Barcode;
    env.operationId = 7;
    const std::string data220(220, 'A');
    const std::string bodyStr = "{\"schema\":\"esbg.control/2.0\",\"name\":\"barcode.generate\",\"body\":{\"type\":\"qr\",\"data\":\"" +
                                 data220 + "\",\"display\":true}}";
    const std::vector<uint8_t> body(bodyStr.begin(), bodyStr.end());
    std::vector<uint8_t> message;
    CodecError encError;
    CHECK(encodeEnvelope(env, body, message, encError));

    constexpr std::size_t kMaxPayload = 214;
    CHECK(message.size() == 353);

    HopFrameHeader h0;
    h0.trafficClass = TrafficClass::Critical;  // arbitrary for this vector, exercises field round-trip only
    h0.profileId = CarrierProfileId::EspNowV1;
    h0.linkSessionId = 2002;
    h0.linkMessageId = 5;
    h0.fragmentIndex = 0;
    h0.fragmentCount = 2;
    HopFrameHeader h1 = h0;
    h1.fragmentIndex = 1;

    std::vector<uint8_t> frame0, frame1;
    CodecError e0, e1;
    CHECK(encodeHopFrame(h0, message.data(), kMaxPayload, frame0, e0));
    CHECK(encodeHopFrame(h1, message.data() + kMaxPayload, message.size() - kMaxPayload, frame1, e1));

    HopFrameHeader dh0, dh1;
    std::vector<uint8_t> p0, p1;
    CodecError de0, de1;
    CHECK(decodeHopFrame(frame0.data(), frame0.size(), dh0, p0, de0));
    CHECK(decodeHopFrame(frame1.data(), frame1.size(), dh1, p1, de1));

    FrameAssembler assembler;
    std::vector<uint8_t> assembled;
    CHECK(assembler.addFragment(dh0, p0, assembled) == AssemblyOutcome::Incomplete);
    CHECK(assembler.addFragment(dh1, p1, assembled) == AssemblyOutcome::Complete);
    CHECK(assembled == message);
}

void test_frame_assembler_ignores_exact_duplicate_fragment() {
    HopFrameHeader h;
    h.linkSessionId = 1; h.linkMessageId = 1; h.fragmentIndex = 0; h.fragmentCount = 2;
    std::vector<uint8_t> payload = {1, 2, 3};
    FrameAssembler assembler;
    std::vector<uint8_t> assembled;
    CHECK(assembler.addFragment(h, payload, assembled) == AssemblyOutcome::Incomplete);
    CHECK(assembler.addFragment(h, payload, assembled) == AssemblyOutcome::DuplicateIgnored);
}

void test_frame_assembler_flags_conflicting_duplicate_fragment() {
    HopFrameHeader h;
    h.linkSessionId = 1; h.linkMessageId = 2; h.fragmentIndex = 0; h.fragmentCount = 2;
    FrameAssembler assembler;
    std::vector<uint8_t> assembled;
    CHECK(assembler.addFragment(h, {1, 2, 3}, assembled) == AssemblyOutcome::Incomplete);
    CHECK(assembler.addFragment(h, {9, 9, 9}, assembled) == AssemblyOutcome::Conflict);
}

void test_frame_assembler_bounds_concurrent_messages() {
    FrameAssembler assembler(/*maxConcurrentMessages=*/2);
    std::vector<uint8_t> assembled;
    HopFrameHeader h1; h1.linkSessionId = 1; h1.linkMessageId = 1; h1.fragmentIndex = 0; h1.fragmentCount = 2;
    HopFrameHeader h2; h2.linkSessionId = 1; h2.linkMessageId = 2; h2.fragmentIndex = 0; h2.fragmentCount = 2;
    HopFrameHeader h3; h3.linkSessionId = 1; h3.linkMessageId = 3; h3.fragmentIndex = 0; h3.fragmentCount = 2;
    CHECK(assembler.addFragment(h1, {1}, assembled) == AssemblyOutcome::Incomplete);
    CHECK(assembler.addFragment(h2, {2}, assembled) == AssemblyOutcome::Incomplete);
    // A third concurrent message evicts message 1 (FIFO) rather than growing unbounded.
    CHECK(assembler.addFragment(h3, {3}, assembled) == AssemblyOutcome::Incomplete);
    HopFrameHeader h1b = h1; h1b.fragmentIndex = 1;
    // Message 1's second fragment now targets an evicted/rebuilt slot — must not silently
    // "complete" with wrong data; a fresh single-fragment index-1-of-2 restart is Incomplete.
    CHECK(assembler.addFragment(h1b, {1, 1}, assembled) == AssemblyOutcome::Incomplete);
}

}  // namespace

int main() {
    test_envelope_encode_matches_vector_a();
    test_envelope_decode_round_trips_vector_a();
    test_envelope_rejects_truncated_input();
    test_hop_frame_encode_matches_vector_b();
    test_hop_frame_decode_validates_crc_and_recovers_payload();
    test_hop_frame_decode_rejects_corrupted_payload();
    test_cobs_round_trips_a_frame_containing_zero_bytes();
    test_cobs_rejects_a_literal_zero_inside_the_block();
    test_cobs_round_trips_data_ending_in_zero();
    test_cobs_round_trips_a_lone_zero_byte();
    test_cobs_round_trips_254_byte_run_followed_by_zero();
    test_frame_assembler_reassembles_two_fragments_in_order();
    test_frame_assembler_ignores_exact_duplicate_fragment();
    test_frame_assembler_flags_conflicting_duplicate_fragment();
    test_frame_assembler_bounds_concurrent_messages();
    if (failures != 0) {
        std::cerr << failures << " esplink codec test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink codec tests passed\n";
    return EXIT_SUCCESS;
}
