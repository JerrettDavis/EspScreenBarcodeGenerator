#include "Fragmenter.h"

#include <iostream>
#include <numeric>

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

HopFrameHeader makeTemplateHeader() {
    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::EspNowV1;
    header.linkSessionId = 7;
    header.linkMessageId = 42;
    return header;
}

void test_rejects_empty_message() {
    const HopFrameHeader header = makeTemplateHeader();
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    CHECK(!fragmentIntoHopFrames(nullptr, 0, header, 250, frames, error));
    CHECK(error == CodecError::TooShort);
    CHECK(frames.empty());
}

void test_rejects_frame_ceiling_too_small_for_overhead() {
    const HopFrameHeader header = makeTemplateHeader();
    const std::vector<uint8_t> message{1, 2, 3};
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    CHECK(!fragmentIntoHopFrames(message.data(), message.size(), header, kHopFrameOverhead, frames, error));
    CHECK(error == CodecError::PayloadTooLarge);
}

void test_single_fragment_when_message_fits() {
    const HopFrameHeader header = makeTemplateHeader();
    const std::vector<uint8_t> message{10, 20, 30, 40};
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    CHECK(fragmentIntoHopFrames(message.data(), message.size(), header, 250, frames, error));
    CHECK(error == CodecError::None);
    CHECK(frames.size() == 1);
    CHECK(frames[0].size() == kHopFrameOverhead + message.size());

    HopFrameHeader decodedHeader;
    std::vector<uint8_t> payload;
    CodecError decodeError;
    CHECK(decodeHopFrame(frames[0].data(), frames[0].size(), decodedHeader, payload, decodeError));
    CHECK(decodeError == CodecError::None);
    CHECK(decodedHeader.fragmentIndex == 0);
    CHECK(decodedHeader.fragmentCount == 1);
    CHECK(decodedHeader.linkSessionId == 7);
    CHECK(decodedHeader.linkMessageId == 42);
    CHECK(decodedHeader.profileId == CarrierProfileId::EspNowV1);
    CHECK(payload == message);
}

void test_splits_across_espnow_v1_ceiling_and_reassembles() {
    // ESP-NOW v1's 250-byte datagram ceiling leaves 214 bytes of payload per frame
    // (kHopFrameOverhead = 36). A 500-byte message must split into 3 fragments.
    const HopFrameHeader header = makeTemplateHeader();
    std::vector<uint8_t> message(500);
    std::iota(message.begin(), message.end(), uint8_t{0});  // 0,1,2,...,255,0,1,...

    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    CHECK(fragmentIntoHopFrames(message.data(), message.size(), header, 250, frames, error));
    CHECK(error == CodecError::None);
    CHECK(frames.size() == 3);  // ceil(500/214) == 3

    std::vector<uint8_t> reassembled;
    for (const auto& frame : frames) {
        CHECK(frame.size() <= 250);
        HopFrameHeader decodedHeader;
        std::vector<uint8_t> payload;
        CodecError decodeError;
        CHECK(decodeHopFrame(frame.data(), frame.size(), decodedHeader, payload, decodeError));
        CHECK(decodeError == CodecError::None);
        CHECK(decodedHeader.fragmentCount == 3);
        reassembled.insert(reassembled.end(), payload.begin(), payload.end());
    }
    CHECK(reassembled == message);
}

void test_fragment_indices_are_sequential() {
    const HopFrameHeader header = makeTemplateHeader();
    const std::vector<uint8_t> message(1000, 0xAB);
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    CHECK(fragmentIntoHopFrames(message.data(), message.size(), header, 250, frames, error));
    CHECK(error == CodecError::None);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        HopFrameHeader decodedHeader;
        std::vector<uint8_t> payload;
        CodecError decodeError;
        CHECK(decodeHopFrame(frames[i].data(), frames[i].size(), decodedHeader, payload, decodeError));
        CHECK(decodedHeader.fragmentIndex == i);
    }
}

}  // namespace

int main() {
    test_rejects_empty_message();
    test_rejects_frame_ceiling_too_small_for_overhead();
    test_single_fragment_when_message_fits();
    test_splits_across_espnow_v1_ceiling_and_reassembles();
    test_fragment_indices_are_sequential();
    if (failures != 0) {
        std::cerr << failures << " esplink fragmenter test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink fragmenter tests passed\n";
    return EXIT_SUCCESS;
}
