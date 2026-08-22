#include "CommandCatalog.h"
#include "ConnectivityTypes.h"
#include "Identifiers.h"
#include "ProtocolCommands.h"
#include "ScreenOrientation.h"

#include <cstring>
#include <iostream>

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

int main() {
    CHECK(kCommandCatalogSize == 19);
    CHECK(findCommandDescriptor("generate") != nullptr);
    CHECK(findCommandDescriptor("generate")->trafficClass == TrafficClass::Control);
    CHECK(findCommandDescriptor("upload_chunk")->trafficClass == TrafficClass::Bulk);
    CHECK(findCommandDescriptor("orientation") != nullptr);
    CHECK(findCommandDescriptor("orientation")->trafficClass == TrafficClass::Control);
    CHECK(findCommandDescriptor("nonexistent") == nullptr);

    Command cmd = GenerateCommand{};
    CHECK(std::holds_alternative<GenerateCommand>(cmd));

    Response resp = SimpleOkResponse{"close", "barcode closed"};
    CHECK(std::holds_alternative<SimpleOkResponse>(resp));

    OperationId a{1};
    OperationId b{1};
    OperationId c{2};
    CHECK(a == b);
    CHECK(a != c);

    static_assert(static_cast<uint8_t>(MessageKind::Command) == 0);
    static_assert(static_cast<uint8_t>(ServiceId::Barcode) == 1);
    static_assert(static_cast<uint8_t>(CarrierProfileId::StreamStandard) == 4);
    static_assert(static_cast<uint8_t>(TrafficClass::Critical) == 3);
    static_assert(static_cast<uint8_t>(FrameType::Reset) == 5);

    ModeTransition transition{RunMode::UsbV1, RunMode::UsbV2, ModeTransitionReason::UserRequested};
    CHECK(transition.from == RunMode::UsbV1);
    CHECK(transition.to == RunMode::UsbV2);
    FallbackPolicy policy = FallbackPolicy::ConnectFailure;
    CHECK(policy == FallbackPolicy::ConnectFailure);

    // rotateNativeTouchPoint: native (rotation-0) touch coordinates re-mapped
    // into each live rotation's coordinate space, derived from the ST7796
    // driver's actual MADCTL bit usage per rotation (see ScreenOrientation.h).
    // Native panel is 320 wide x 480 tall (TFT_WIDTH x TFT_HEIGHT).
    {
        constexpr int kW = 320, kH = 480;

        // Deg0 is the identity mapping (the calibration's own rotation).
        auto p = rotateNativeTouchPoint(0, 0, ScreenOrientation::Deg0, kW, kH);
        CHECK(p.x == 0 && p.y == 0);
        p = rotateNativeTouchPoint(319, 479, ScreenOrientation::Deg0, kW, kH);
        CHECK(p.x == 319 && p.y == 479);

        // Deg90/Deg270 land in the swapped 480x320 landscape space; every
        // native corner must map inside that space's bounds.
        for (int nx : {0, 319}) {
            for (int ny : {0, 479}) {
                auto p90 = rotateNativeTouchPoint(nx, ny, ScreenOrientation::Deg90, kW, kH);
                CHECK(p90.x >= 0 && p90.x < kH);
                CHECK(p90.y >= 0 && p90.y < kW);
                auto p270 = rotateNativeTouchPoint(nx, ny, ScreenOrientation::Deg270, kW, kH);
                CHECK(p270.x >= 0 && p270.x < kH);
                CHECK(p270.y >= 0 && p270.y < kW);
            }
        }

        // Deg90 and Deg270 are inverses of each other's corner assignment
        // (a 180-degree difference), and neither collapses distinct native
        // corners onto the same rotated point (i.e. the mapping stays a
        // bijection over the 4 corners, not just an in-bounds clamp).
        auto p90a = rotateNativeTouchPoint(0, 0, ScreenOrientation::Deg90, kW, kH);
        auto p90b = rotateNativeTouchPoint(319, 0, ScreenOrientation::Deg90, kW, kH);
        auto p90c = rotateNativeTouchPoint(0, 479, ScreenOrientation::Deg90, kW, kH);
        auto p90d = rotateNativeTouchPoint(319, 479, ScreenOrientation::Deg90, kW, kH);
        CHECK(!(p90a.x == p90b.x && p90a.y == p90b.y));
        CHECK(!(p90a.x == p90c.x && p90a.y == p90c.y));
        CHECK(!(p90a.x == p90d.x && p90a.y == p90d.y));
        CHECK(!(p90b.x == p90c.x && p90b.y == p90c.y));
        CHECK(!(p90b.x == p90d.x && p90b.y == p90d.y));
        CHECK(!(p90c.x == p90d.x && p90c.y == p90d.y));

        // Deg180 mirrors both axes within the native (portrait) bounds.
        auto p180 = rotateNativeTouchPoint(0, 0, ScreenOrientation::Deg180, kW, kH);
        CHECK(p180.x == 319 && p180.y == 479);
        p180 = rotateNativeTouchPoint(319, 479, ScreenOrientation::Deg180, kW, kH);
        CHECK(p180.x == 0 && p180.y == 0);
    }

    if (failures != 0) {
        std::cerr << failures << " esplink type test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink type tests passed\n";
    return EXIT_SUCCESS;
}
