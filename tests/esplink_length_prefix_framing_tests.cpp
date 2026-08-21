#include "LengthPrefixFraming.h"

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

void test_encode_roundtrips_through_a_single_feed() {
    const std::vector<uint8_t> payload{1, 2, 3, 4, 5};
    const std::vector<uint8_t> encoded = encodeLengthPrefixedFrame(payload);
    CHECK(encoded.size() == payload.size() + 4);

    LengthPrefixFrameParser parser(1024);
    std::vector<std::vector<uint8_t>> frames;
    CHECK(parser.feed(encoded.data(), encoded.size(), frames));
    CHECK(!parser.hasError());
    CHECK(frames.size() == 1);
    CHECK(frames[0] == payload);
}

void test_feeds_one_byte_at_a_time() {
    const std::vector<uint8_t> payload{10, 20, 30, 40, 50, 60, 70};
    const std::vector<uint8_t> encoded = encodeLengthPrefixedFrame(payload);

    LengthPrefixFrameParser parser(1024);
    std::vector<std::vector<uint8_t>> frames;
    for (const uint8_t byte : encoded) {
        CHECK(parser.feed(&byte, 1, frames));
    }
    CHECK(!parser.hasError());
    CHECK(frames.size() == 1);
    CHECK(frames[0] == payload);
}

void test_multiple_frames_in_one_feed() {
    const std::vector<uint8_t> first{1, 2, 3};
    const std::vector<uint8_t> second{9, 8, 7, 6};
    std::vector<uint8_t> combined = encodeLengthPrefixedFrame(first);
    const std::vector<uint8_t> secondEncoded = encodeLengthPrefixedFrame(second);
    combined.insert(combined.end(), secondEncoded.begin(), secondEncoded.end());

    LengthPrefixFrameParser parser(1024);
    std::vector<std::vector<uint8_t>> frames;
    CHECK(parser.feed(combined.data(), combined.size(), frames));
    CHECK(frames.size() == 2);
    CHECK(frames[0] == first);
    CHECK(frames[1] == second);
}

void test_rejects_zero_length() {
    const std::vector<uint8_t> zeroLength{0, 0, 0, 0};
    LengthPrefixFrameParser parser(1024);
    std::vector<std::vector<uint8_t>> frames;
    CHECK(!parser.feed(zeroLength.data(), zeroLength.size(), frames));
    CHECK(parser.hasError());
    CHECK(frames.empty());
}

void test_rejects_oversized_length() {
    const std::vector<uint8_t> huge{0xFF, 0xFF, 0xFF, 0x00};  // 16,777,215
    LengthPrefixFrameParser parser(1024);
    std::vector<std::vector<uint8_t>> frames;
    CHECK(!parser.feed(huge.data(), huge.size(), frames));
    CHECK(parser.hasError());
}

void test_stays_errored_until_reset() {
    const std::vector<uint8_t> zeroLength{0, 0, 0, 0};
    LengthPrefixFrameParser parser(1024);
    std::vector<std::vector<uint8_t>> frames;
    CHECK(!parser.feed(zeroLength.data(), zeroLength.size(), frames));
    CHECK(parser.hasError());

    const std::vector<uint8_t> valid = encodeLengthPrefixedFrame({1, 2, 3});
    CHECK(!parser.feed(valid.data(), valid.size(), frames));  // still errored, ignores new input
    CHECK(frames.empty());

    parser.reset();
    CHECK(!parser.hasError());
    CHECK(parser.feed(valid.data(), valid.size(), frames));
    CHECK(frames.size() == 1);
}

void test_large_frame_near_ceiling() {
    std::vector<uint8_t> payload(4096);
    std::iota(payload.begin(), payload.end(), uint8_t{0});
    const std::vector<uint8_t> encoded = encodeLengthPrefixedFrame(payload);

    LengthPrefixFrameParser parser(4096);
    std::vector<std::vector<uint8_t>> frames;
    // Split the feed into TCP-MSS-sized chunks to mimic real socket reads.
    std::size_t offset = 0;
    while (offset < encoded.size()) {
        const std::size_t chunk = std::min<std::size_t>(1460, encoded.size() - offset);
        CHECK(parser.feed(encoded.data() + offset, chunk, frames));
        offset += chunk;
    }
    CHECK(frames.size() == 1);
    CHECK(frames[0] == payload);
}

}  // namespace

int main() {
    test_encode_roundtrips_through_a_single_feed();
    test_feeds_one_byte_at_a_time();
    test_multiple_frames_in_one_feed();
    test_rejects_zero_length();
    test_rejects_oversized_length();
    test_stays_errored_until_reset();
    test_large_frame_near_ceiling();
    if (failures != 0) {
        std::cerr << failures << " esplink length-prefix framing test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink length-prefix framing tests passed\n";
    return EXIT_SUCCESS;
}
