#include "CommandCatalog.h"
#include "ConnectivityTypes.h"
#include "Identifiers.h"
#include "ProtocolCommands.h"

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
    CHECK(kCommandCatalogSize == 18);
    CHECK(findCommandDescriptor("generate") != nullptr);
    CHECK(findCommandDescriptor("generate")->trafficClass == TrafficClass::Control);
    CHECK(findCommandDescriptor("upload_chunk")->trafficClass == TrafficClass::Bulk);
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

    if (failures != 0) {
        std::cerr << failures << " esplink type test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink type tests passed\n";
    return EXIT_SUCCESS;
}
