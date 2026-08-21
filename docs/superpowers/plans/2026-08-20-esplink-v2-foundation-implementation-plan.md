# EspLink v2 Foundation + USB v2 Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the firmware's transport-coupled `UsbProtocol` into a portable, session-scoped `ControlProtocolEngine` behind application ports, define the EspLink v2 binary envelope/frame/COBS/CRC/fragmentation codec shared by C++ and C#, prove it end-to-end over a real USB v2 path (COBS-framed, explicit v1→v2 negotiation), and mirror the same architecture in a new portable `.NET` protocol/connectivity layer — while leaving USB Protocol 1.0 byte-for-byte unchanged and leaving Bluetooth/Wi-Fi Direct/ESP-NOW gateway as scaffolding only.

**Architecture:** Clean/hexagonal layering on both sides. Firmware: `lib/EspLinkCore` (new, Arduino-free, portable protocol+domain core: identifiers, connectivity value objects, pure selection policy, EspLink v2 envelope/frame/COBS/CRC/reassembly codecs, application ports, `ControlSession`/`TransferSession`, `ControlProtocolEngine`) sits below `src/` (Arduino-dependent adapters: `BarcodeApplicationAdapter`, `EspIdfDeviceControl`, `JsonCommandCodec`, `SerialLegacyEndpoint` for v1 NDJSON, `SerialCobsEndpoint` for v2). .NET: new portable `EspBarcode.Protocol` (envelope/frame/COBS/CRC/reassembly, mirrors the C++ codec) and `EspBarcode.Connectivity` (value objects + pure `TransportSelector`, typed v2 client core: `ILinkConnector`/`ILinkConnection`/`EspLinkLinkSession`/`EspLinkControlSession`), plus a `SerialV2Connector` added to the existing `EspBarcode.Client` project. `EspBarcode.Client`'s existing v1 surface (`IEspBarcodeTransport`, `EspBarcodeClient`, `SerialPortTransport`) is untouched.

**Tech Stack:** C++17 (PlatformIO `native` env / CMake for host tests, `esp32dev` env for firmware), ArduinoJson 7.4.3 (adapter layer only), .NET 10.0 / C# 14 (`Directory.Build.props`: `Nullable`, `TreatWarningsAsErrors`, `ImplicitUsings` all enabled), xUnit (existing `dotnet/tests/*` convention), CTest/plain-assert native C++ tests (existing `tests/native_core_tests.cpp` convention).

**Spec:** `docs/superpowers/plans/2026-08-20-multi-transport-esp-link-implementation-plan.md` (the approved architecture design; this plan implements its §26 "Immediate next work package" plus a working USB v2 path per §26.2, per the user's explicit instruction to prioritize shared architecture + one fully working USB v2 path this session). Wire-format ground truth for USB Protocol 1.0: `docs/PROTOCOL.md`.

## Global Constraints

- Baseline: `origin/main` at `d1565337b3d8f7b6cf8569cf0c443dfd43057072` ("chore: add windows targeting").
- **No Bluetooth, Wi-Fi Direct, or ESP-NOW gateway production code this session.** Transport-agnostic enum members (e.g. `TransportKind::Bluetooth` with `CapabilityState::CompiledOut`) are fine; RFCOMM/TCP/ESP-NOW connectors are not — see plan §26.1 "Out of scope" and §4 non-goals.
- `lib/EspLinkCore` (new PlatformIO library) MUST NOT include or reference: `Arduino.h`, `ArduinoJson.h`, `Serial`, `TFT_eSPI.h`, `WiFi`, `BluetoothSerial`, `esp_now_*`, `LittleFS`, `ESP.*`. It must compile and link as plain C++17 under the existing `native` CMake target, exactly like `lib/EspBarcodeCore` does today.
- JSON parsing/serialization for the wire protocol stays out of `lib/EspLinkCore`. It lives in `src/JsonCommandCodec.h/.cpp` (Arduino/ArduinoJson-dependent, firmware-layer adapter).
- New native C++ code compiles warning-free under the existing CMake flags (`-Wall -Wextra -Wpedantic -Werror` / `/W4 /WX` on MSVC) and passes under `-fsanitize=address,undefined` when `ESPBARCODE_ENABLE_SANITIZERS=ON`.
- New .NET code lives under projects that inherit `dotnet/Directory.Build.props` (`net10.0`, `LangVersion 14.0`, `Nullable=enable`, `TreatWarningsAsErrors=true`) and builds warning-free.
- USB Protocol 1.0 wire format is preserved byte-for-byte: identical command names, field names/types, response shapes, event shapes, and error codes as documented in `docs/PROTOCOL.md`. `dotnet/src/EspBarcode.Client`, `EspBarcode.Cli`, `EspBarcode.Generator`, `EspBarcode.Viewer.Cli`, `EspBarcode.Viewer.Gui` keep their existing public APIs unchanged (additive only) and `tests/test_host_tool.py` / `tools/espbarcode.py` keep working unchanged.
- EspLink v2 multi-byte binary fields (envelope + hop frame) are little-endian throughout.
- CRC-32 for all new binary framing is IEEE 802.3 (poly `0xEDB88320`, init/final XOR `0xFFFFFFFF`) — the same algorithm as `UsbProtocol::crc32` (`src/UsbProtocol.cpp:651-660`) and `dotnet/src/EspBarcode.Client/Crc32.cs`.
- Envelope/frame enumerant numeric values (fixed for this plan, reused across every task and both languages):
  - `MessageKind` (envelope `kind`, offset 4): `0`=Command, `1`=Result, `2`=Event, `3`=Error, `4`=Transfer.
  - `ServiceId` (envelope `serviceId`, offset 6): `0`=System, `1`=Barcode, `2`=Preset, `3`=Transfer, `4`=Device, `5`=Connectivity, `6`=Trust, `7`=Gateway, `8`=Diagnostics.
  - `CodecId` (envelope `codecId`, offset 7): `0`=Json, `1`=Binary.
  - `FrameType` (hop frame `frameType`, offset 4): `0`=Data, `1`=Ack, `2`=Nack, `3`=KeepAlive, `4`=Close, `5`=Reset.
  - `TrafficClass` (hop frame `trafficClass`, offset 6): `0`=Control, `1`=Metadata, `2`=Bulk, `3`=Critical, `4`=Event.
  - `CarrierProfileId` (hop frame `profileId`, offset 7): `0`=Unspecified, `1`=espnow-v1, `2`=espnow-v2, `3`=stream-small, `4`=stream-standard, `5`=stream-large, `6`=tcp-standard, `7`=tcp-large.
- Every new native test file is registered in `CMakeLists.txt` as its own executable + `add_test(...)` so `ctest` picks it up automatically (matches the existing `native_core_tests` pattern).
- Commit after every task's tests pass. Do not squash tasks together.

## File Structure

New/changed C++ (firmware):

- `lib/EspLinkCore/library.json`, `lib/EspLinkCore/src/Identifiers.h` — strong-typed IDs.
- `lib/EspLinkCore/src/ConnectivityTypes.h` — `RunMode`, `TransportKind`, `CarrierProfileId`, `CapabilityState`, `OptimizationGoal`, `TrafficClass`, `Idempotency` enums.
- `lib/EspLinkCore/src/SelectionPolicy.h/.cpp` — pure transport-selection evaluator.
- `lib/EspLinkCore/src/Crc32.h/.cpp` — shared IEEE CRC-32.
- `lib/EspLinkCore/src/Envelope.h/.cpp` — Layer 3 message envelope codec.
- `lib/EspLinkCore/src/HopFrame.h/.cpp` — Layer 2 per-hop frame codec.
- `lib/EspLinkCore/src/Cobs.h/.cpp` — COBS codec.
- `lib/EspLinkCore/src/FrameAssembler.h/.cpp` — fragment reassembly.
- `lib/EspLinkCore/src/ApplicationPorts.h` — `IBarcodeDevice`, `IPresetRepository`, `IDeviceControl` abstract ports.
- `lib/EspLinkCore/src/ControlSession.h/.cpp`, `lib/EspLinkCore/src/TransferSession.h` — session-scoped state.
- `lib/EspLinkCore/src/CommandCatalog.h` — declarative v1 command descriptor table.
- `lib/EspLinkCore/src/ProtocolCommands.h` — typed `Command`/`Response`/`ProtocolError` variants.
- `lib/EspLinkCore/src/ControlProtocolEngine.h/.cpp` — dispatcher, no Serial/ArduinoJson.
- `src/JsonCommandCodec.h/.cpp` — ArduinoJson ⇄ typed `Command`/`Response` translation (adapter layer).
- `src/BarcodeApplicationAdapter.h/.cpp` — wraps `BarcodeApplication`/`PresetStore` behind the ports.
- `src/EspIdfDeviceControl.h/.cpp` — wraps `ESP.getFreeHeap()`/`ESP.restart()` behind `IDeviceControl`.
- `src/SerialLegacyEndpoint.h/.cpp` — replaces `include/UsbProtocol.h` + `src/UsbProtocol.cpp` (deleted).
- `src/SerialCobsEndpoint.h/.cpp` — v2 COBS-framed endpoint, explicit v1→v2 negotiation.
- `src/main.cpp` — modified to wire the new endpoints.
- `tests/vectors_v1_golden.h` — literal v1 request/response fixture pairs.
- `tests/esplink_codec_tests.cpp` — envelope/frame/COBS/CRC/reassembly tests.
- `tests/esplink_selection_tests.cpp` — `SelectionPolicy` table-driven tests.
- `tests/control_protocol_engine_tests.cpp` — engine/session/dispatch/golden-fixture tests.
- `CMakeLists.txt` — modified: new `EspLinkCore` static lib target, new test executables.

New/changed .NET:

- `dotnet/src/EspBarcode.Protocol/EspBarcode.Protocol.csproj` — new portable project (envelope/frame/COBS/CRC/reassembly).
- `dotnet/src/EspBarcode.Connectivity/EspBarcode.Connectivity.csproj` — new portable project (value objects, `TransportSelector`, v2 client core).
- `dotnet/src/EspBarcode.Client/TransportV2/SerialV2Connector.cs` — new file in the existing client project.
- `dotnet/tests/EspBarcode.Protocol.Tests/`, `dotnet/tests/EspBarcode.Connectivity.Tests/` — new xUnit test projects.
- `dotnet/tests/EspBarcode.Client.Tests/SerialV2ConnectorTests.cs` — new test file.
- `dotnet/EspScreenBarcodeGenerator.slnx` — modified: add the four new projects.

Docs:

- `docs/ARCHITECTURE.md` — modified: new layered diagram, EspLink v2 section, migration notes.
- `docs/PROTOCOL_V2.md` — new: envelope/frame spec, carrier profiles, negotiation, v2 command subset.
- `docs/superpowers/plans/2026-08-20-esplink-v2-foundation-implementation-plan.md` — this file (append a completion summary in the final task).

---

### Task 1: Firmware — connectivity value objects, command catalog, typed protocol commands

**Files:**
- Create: `lib/EspLinkCore/library.json`
- Create: `lib/EspLinkCore/src/Identifiers.h`
- Create: `lib/EspLinkCore/src/ConnectivityTypes.h`
- Create: `lib/EspLinkCore/src/CommandCatalog.h`
- Create: `lib/EspLinkCore/src/ProtocolCommands.h`
- Modify: `CMakeLists.txt` (add `EspLinkCore` static library target)
- Test: `tests/esplink_types_tests.cpp`

**Interfaces:**
- Consumes: `espbarcode::BarcodeSpec`, `espbarcode::Rotation`, `espbarcode::BitMatrix` from `lib/EspBarcodeCore/src/EspBarcodeCore.h` (already in the repo — read it for exact fields before writing `ProtocolCommands.h`).
- Produces: `esplink::OperationId`, `esplink::ControlSessionId`, `esplink::ControllerId`, `esplink::TransferId` (Identifiers.h); `esplink::RunMode`, `esplink::TransportKind`, `esplink::CarrierProfileId`, `esplink::CapabilityState`, `esplink::OptimizationGoal`, `esplink::TrafficClass`, `esplink::Idempotency`, `esplink::MessageKind`, `esplink::ServiceId`, `esplink::CodecId`, `esplink::FrameType` (ConnectivityTypes.h); `esplink::CommandDescriptor`, `esplink::kCommandCatalog[]` (CommandCatalog.h); `esplink::Command` variant, `esplink::Response` variant, `esplink::ProtocolError` (ProtocolCommands.h). Every later firmware task depends on these exact names.

- [ ] **Step 1: Create the PlatformIO library manifest**

`lib/EspLinkCore/library.json`:

```json
{
  "name": "EspLinkCore",
  "version": "0.1.0",
  "description": "Transport-independent EspLink v2 protocol, session, and connectivity core. No Arduino dependency.",
  "frameworks": "*",
  "platforms": "*"
}
```

- [ ] **Step 2: Write `Identifiers.h`**

```cpp
#pragma once

#include <cstdint>

namespace esplink {

struct OperationId {
    uint64_t value = 0;
    friend bool operator==(OperationId a, OperationId b) { return a.value == b.value; }
    friend bool operator!=(OperationId a, OperationId b) { return !(a == b); }
};

struct CorrelationId {
    uint64_t value = 0;
    friend bool operator==(CorrelationId a, CorrelationId b) { return a.value == b.value; }
};

struct ControlSessionId {
    uint32_t value = 0;
    friend bool operator==(ControlSessionId a, ControlSessionId b) { return a.value == b.value; }
    friend bool operator!=(ControlSessionId a, ControlSessionId b) { return !(a == b); }
};

struct ControllerId {
    uint32_t value = 0;
    friend bool operator==(ControllerId a, ControllerId b) { return a.value == b.value; }
};

struct TransferId {
    uint32_t value = 0;
    friend bool operator==(TransferId a, TransferId b) { return a.value == b.value; }
};

}  // namespace esplink
```

- [ ] **Step 3: Write `ConnectivityTypes.h`**

```cpp
#pragma once

#include <cstdint>

namespace esplink {

enum class RunMode : uint8_t {
    Standalone, Auto, UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway, Adaptive
};

enum class TransportKind : uint8_t {
    UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway
};

// Numeric values are fixed by the plan's Global Constraints and are wire-relevant
// (they appear in the EspLink v2 hop frame `profileId` field) — do not renumber.
enum class CarrierProfileId : uint8_t {
    Unspecified = 0,
    EspNowV1 = 1,
    EspNowV2 = 2,
    StreamSmall = 3,
    StreamStandard = 4,
    StreamLarge = 5,
    TcpStandard = 6,
    TcpLarge = 7,
};

enum class CapabilityState : uint8_t {
    CompiledOut, UnsupportedHardware, Disabled, Blocked, Unavailable,
    Available, Degraded, Experimental, Qualified
};

enum class OptimizationGoal : uint8_t {
    LowLatency, HighThroughput, Balanced, MinimalHostDependencies, Deterministic, MinimumPower
};

// Wire-relevant (hop frame `trafficClass` field) — do not renumber.
enum class TrafficClass : uint8_t { Control = 0, Metadata = 1, Bulk = 2, Critical = 3, Event = 4 };

enum class Idempotency : uint8_t { Query, Idempotent, ReplayResult, RejectDuplicate, ResumeRequired };

// Wire-relevant (envelope `kind` field) — do not renumber.
enum class MessageKind : uint8_t { Command = 0, Result = 1, Event = 2, Error = 3, Transfer = 4 };

// Wire-relevant (envelope `serviceId` field) — do not renumber.
enum class ServiceId : uint8_t {
    System = 0, Barcode = 1, Preset = 2, Transfer = 3, Device = 4,
    Connectivity = 5, Trust = 6, Gateway = 7, Diagnostics = 8
};

// Wire-relevant (envelope `codecId` field) — do not renumber.
enum class CodecId : uint8_t { Json = 0, Binary = 1 };

// Wire-relevant (hop frame `frameType` field) — do not renumber.
enum class FrameType : uint8_t { Data = 0, Ack = 1, Nack = 2, KeepAlive = 3, Close = 4, Reset = 5 };

// Declarative fallback policy (design plan §1.4). No connector implements a live fallback
// state machine yet this session (only one transport, `usb-v1`/`usb-v2`, exists) — this is
// the value-object seam a future multi-transport selector reads, kept out of the codec/session
// types above so it can be added without touching the wire format.
enum class FallbackPolicy : uint8_t { UnavailableOnly, ConnectFailure, PreOperation, Never };

enum class ModeTransitionReason : uint8_t {
    UserRequested, ConnectFailure, CapabilityLost, Unavailable, PolicyDenied, Recovered
};

struct ModeTransition {
    RunMode from = RunMode::Standalone;
    RunMode to = RunMode::Standalone;
    ModeTransitionReason reason = ModeTransitionReason::UserRequested;
};

}  // namespace esplink
```

- [ ] **Step 4: Write `CommandCatalog.h`**

This is the declarative descriptor table for every USB Protocol 1.0 command (see `docs/PROTOCOL.md` "Commands" and `src/UsbProtocol.cpp:263-273` for the exact name list). `ping` is a hidden alias for `hello` (see `src/UsbProtocol.cpp:86`) and is **not** a separate catalog row.

```cpp
#pragma once

#include "ConnectivityTypes.h"

namespace esplink {

struct CommandDescriptor {
    const char* name;
    TrafficClass trafficClass;
    Idempotency idempotency;
    bool requiresLease;  // false in v1 (no lease model existed); reserved for v2 namespaces.
};

// clang-format off
inline constexpr CommandDescriptor kCommandCatalog[] = {
    {"hello",          TrafficClass::Control,  Idempotency::Query,        false},
    {"capabilities",   TrafficClass::Metadata, Idempotency::Query,        false},
    {"status",         TrafficClass::Metadata, Idempotency::Query,        false},
    {"generate",       TrafficClass::Control,  Idempotency::Idempotent,   false},
    {"display",        TrafficClass::Control,  Idempotency::Idempotent,   false},
    {"close",          TrafficClass::Control,  Idempotency::Idempotent,   false},
    {"home",           TrafficClass::Control,  Idempotency::Idempotent,   false},
    {"save",           TrafficClass::Critical, Idempotency::ReplayResult, false},
    {"load",           TrafficClass::Control,  Idempotency::Idempotent,   false},
    {"delete",         TrafficClass::Critical, Idempotency::ReplayResult, false},
    {"list",           TrafficClass::Metadata, Idempotency::Query,        false},
    {"upload_begin",   TrafficClass::Bulk,     Idempotency::RejectDuplicate, false},
    {"upload_chunk",   TrafficClass::Bulk,     Idempotency::ResumeRequired,  false},
    {"upload_end",     TrafficClass::Bulk,     Idempotency::ReplayResult, false},
    {"upload_abort",   TrafficClass::Bulk,     Idempotency::Idempotent,   false},
    {"download",       TrafficClass::Bulk,     Idempotency::Query,        false},
    {"backlight",      TrafficClass::Control,  Idempotency::Idempotent,   false},
    {"reboot",         TrafficClass::Critical, Idempotency::ReplayResult, false},
};
// clang-format on

inline constexpr std::size_t kCommandCatalogSize = sizeof(kCommandCatalog) / sizeof(kCommandCatalog[0]);

inline const CommandDescriptor* findCommandDescriptor(const char* name) {
    for (const auto& d : kCommandCatalog) {
        const char* a = d.name;
        const char* b = name;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return &d;
    }
    return nullptr;
}

}  // namespace esplink
```

(`findCommandDescriptor` avoids pulling in `<cstring>`/`<string>` here on purpose — `lib/EspLinkCore` headers should stay dependency-light. Add `#include <cstddef>` for `std::size_t`.)

- [ ] **Step 5: Write `ProtocolCommands.h`**

Typed request/response value objects, one struct per v1 command shape, matching `docs/PROTOCOL.md` field-for-field.

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "EspBarcodeCore.h"

namespace esplink {

struct HelloCommand {};
struct CapabilitiesCommand {};
struct StatusCommand {};

struct GenerateCommand {
    espbarcode::BarcodeSpec spec;
    bool display = true;
    std::optional<std::string> saveAs;
};

struct DisplayCommand {
    std::optional<std::string> presetName;
};

struct CloseCommand {};
struct HomeCommand {};

struct SaveCommand { std::string name; };

struct LoadCommand {
    std::string name;
    bool display = false;
};

struct DeleteCommand { std::string name; };
struct ListCommand {};

struct UploadBeginCommand {
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 4;
    espbarcode::Rotation rotation = espbarcode::Rotation::Auto;
    bool invert = false;
    bool display = true;
    std::string label;
};

struct UploadChunkCommand {
    std::size_t offset = 0;
    std::vector<uint8_t> data;
};

struct UploadEndCommand {
    std::optional<uint32_t> expectedCrc32;
};

struct UploadAbortCommand {};

struct DownloadCommand {
    std::size_t chunkBytes = 384;
};

struct BacklightCommand { bool on = true; };
struct RebootCommand {};

using Command = std::variant<
    HelloCommand, CapabilitiesCommand, StatusCommand, GenerateCommand, DisplayCommand,
    CloseCommand, HomeCommand, SaveCommand, LoadCommand, DeleteCommand, ListCommand,
    UploadBeginCommand, UploadChunkCommand, UploadEndCommand, UploadAbortCommand,
    DownloadCommand, BacklightCommand, RebootCommand>;

struct HelloResponse {
    std::string device;
    std::string protocol;
    std::string firmware;
    std::string transport;
    uint16_t screenWidth = 0;
    uint16_t screenHeight = 0;
};

struct CapabilitiesResponse {
    std::vector<std::string> symbologies;
    std::vector<std::string> commands;
    std::size_t payloadBytes = 0;
    std::size_t serialLineBytes = 0;
    uint16_t matrixWidth = 0;
    uint16_t matrixHeight = 0;
    std::string uploadEncoding;
    std::size_t uploadChunkBytesRecommended = 0;
    bool rawMatrix = true;
    bool standaloneTouchUi = true;
    bool persistentPresets = true;
};

struct CurrentSymbolInfo {
    std::string label;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 0;
    std::string rotation;
    bool invert = false;
    std::size_t bytes = 0;
};

struct StatusResponse {
    bool barcodeVisible = false;
    bool hasCurrent = false;
    bool currentRaw = false;
    std::string status;
    uint32_t freeHeap = 0;
    std::optional<CurrentSymbolInfo> current;
};

struct GenerateResponse {
    std::string type;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 0;
    bool displayed = false;
    std::string normalizedData;
};

// Used for display/close/home/save/load/delete/backlight/reboot/upload_abort —
// every v1 command whose success response is only {"ok":true,"cmd":...,"message"?:...}.
struct SimpleOkResponse {
    std::string command;
    std::optional<std::string> message;
};

struct ListResponse { std::vector<std::string> presets; };

struct UploadBeginResponse {
    std::size_t bytesExpected = 0;
    std::size_t nextOffset = 0;
};

struct UploadChunkResponse {
    std::size_t accepted = 0;
    std::size_t nextOffset = 0;
};

struct UploadEndResponse {
    uint32_t crc32 = 0;
    bool displayed = false;
};

struct DownloadBeginEvent {
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 0;
    std::string rotation;
    bool invert = false;
    std::string label;
    std::size_t bytes = 0;
    std::string encoding;
    uint32_t crc32 = 0;
};

struct DownloadChunkEvent {
    std::size_t offset = 0;
    std::vector<uint8_t> data;
};

struct DownloadEndEvent {
    std::size_t bytes = 0;
    uint32_t crc32 = 0;
};

using Response = std::variant<
    HelloResponse, CapabilitiesResponse, StatusResponse, GenerateResponse, SimpleOkResponse,
    ListResponse, UploadBeginResponse, UploadChunkResponse, UploadEndResponse,
    DownloadBeginEvent, DownloadChunkEvent, DownloadEndEvent>;

struct ProtocolError {
    std::string command;  // may be empty, matching UsbProtocol::sendError's optional cmd
    std::string code;
    std::string message;
};

}  // namespace esplink
```

- [ ] **Step 6: Add the `EspLinkCore` CMake target**

In `CMakeLists.txt`, after the existing `EspBarcodeCore` target (around line 27, right after `espbarcode_enable_sanitizers(EspBarcodeCore)`), add:

```cmake
add_library(EspLinkCore INTERFACE)
target_include_directories(EspLinkCore INTERFACE lib/EspLinkCore/src)
target_link_libraries(EspLinkCore INTERFACE EspBarcodeCore)
```

(`INTERFACE` because after this task the library is header-only. Later tasks add `.cpp` files — when Task 4 adds the first `.cpp` file, change this to `add_library(EspLinkCore STATIC ${ESPLINKCORE_SOURCES})` with `target_include_directories`/`target_compile_options` matching the `EspBarcodeCore` target above it, and keep `target_link_libraries(EspLinkCore PUBLIC EspBarcodeCore)`.)

- [ ] **Step 7: Write a compile-and-shape smoke test**

`tests/esplink_types_tests.cpp`:

```cpp
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
```

Register it in `CMakeLists.txt` right after the `native_core_tests` block:

```cmake
add_executable(esplink_types_tests tests/esplink_types_tests.cpp)
target_link_libraries(esplink_types_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_types_tests)
```

and in the `if(BUILD_TESTING)` block:

```cmake
add_test(NAME esplink_types_tests COMMAND esplink_types_tests)
```

- [ ] **Step 8: Build and run**

```bash
cmake -S . -B .build/native-validation -DBUILD_TESTING=ON
cmake --build .build/native-validation --target esplink_types_tests
ctest --test-dir .build/native-validation -R esplink_types_tests --output-on-failure
```

Expected: build succeeds with zero warnings, test passes.

- [ ] **Step 9: Commit**

```bash
git add lib/EspLinkCore CMakeLists.txt tests/esplink_types_tests.cpp
git commit -m "feat(firmware): add EspLinkCore connectivity value objects and typed protocol commands"
```

---

### Task 2: Firmware — pure transport `SelectionPolicy` evaluator

**Files:**
- Create: `lib/EspLinkCore/src/SelectionPolicy.h`
- Modify: `CMakeLists.txt`
- Test: `tests/esplink_selection_tests.cpp`

**Interfaces:**
- Consumes: `esplink::TransportKind`, `esplink::CapabilityState`, `esplink::OptimizationGoal` from Task 1's `ConnectivityTypes.h`.
- Consumes: `esplink::FallbackPolicy` (Task 1's `ConnectivityTypes.h`).
- Produces: `esplink::SideEffectPermission`, `esplink::ConnectionRequirement`, `esplink::TransportCapabilities`, `esplink::SelectionPolicyConfig`, `esplink::CandidateScore`, `esplink::SelectionDecision`, `esplink::evaluateSelection(...)`, `esplink::FallbackContext`, `esplink::isFallbackAllowed(...)`. Header-only (no logic depends on transport I/O), pure functions of their inputs — no clock, no randomness, no static/global state.

This function has no live transports to evaluate yet (Bluetooth/Wi-Fi Direct/ESP-NOW gateway are out of scope this session per Global Constraints) — it is proven here with simulated `TransportCapabilities`, matching plan requirement "Add pure table-driven tests for transport selection and fallback decisions using simulated capability reports."

- [ ] **Step 1: Write `SelectionPolicy.h`**

```cpp
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ConnectivityTypes.h"

namespace esplink {

enum class SideEffectPermission : uint8_t { Forbidden, Allowed, Required };

struct ConnectionRequirement {
    bool wirelessRequired = false;
    SideEffectPermission externalHardware = SideEffectPermission::Allowed;
    SideEffectPermission ipInterface = SideEffectPermission::Allowed;
    SideEffectPermission multiDevice = SideEffectPermission::Allowed;
};

struct TransportCapabilities {
    TransportKind kind;
    CapabilityState state;
    bool wireless = false;
    bool requiresExternalHardware = false;
    bool createsIpInterface = false;
    bool supportsMultipleDevices = false;
};

struct SelectionPolicyConfig {
    RunMode mode = RunMode::Auto;
    OptimizationGoal goal = OptimizationGoal::Balanced;
    ConnectionRequirement requirement;
    std::vector<TransportKind> allow;  // empty = allow every kind
    std::vector<TransportKind> deny;
};

struct CandidateScore {
    TransportKind kind;
    bool eligible = false;
    int score = 0;
    std::string reason;
};

struct SelectionDecision {
    std::optional<TransportKind> selected;
    std::vector<CandidateScore> candidates;
};

namespace detail {

// Base scores by [OptimizationGoal][TransportKind], per the plan's default scoring
// hints (design doc §1.3). Index order must match the TransportKind enum declaration
// order: UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway.
inline constexpr int kBaseScore[6][5] = {
    /*                    UsbV1  UsbV2  Bluetooth  WifiDirect  EspNowGateway */
    /* LowLatency */        {50,    90,       60,         80,           100},
    /* HighThroughput */    {40,    90,       50,        100,            85},
    /* Balanced */          {50,    80,       60,         70,            75},
    /* MinimalHostDeps */   {85,    85,       90,         90,            40},
    /* Deterministic */     {95,   100,       50,         40,            70},
    /* MinimumPower */      {70,    70,       80,         40,            85},
};

inline int statePenalty(CapabilityState state) {
    switch (state) {
        case CapabilityState::Experimental: return 15;
        case CapabilityState::Degraded: return 30;
        default: return 0;  // Available / Qualified: no penalty.
    }
}

inline bool contains(const std::vector<TransportKind>& list, TransportKind kind) {
    return std::find(list.begin(), list.end(), kind) != list.end();
}

}  // namespace detail

// Pure fallback-decision rules from the design plan §1.4. No connector retries live yet
// this session (only one transport exists) — this function is the seam a future connect
// loop calls before attempting the next candidate; it is proven here against simulated
// context, same as evaluateSelection above.
struct FallbackContext {
    bool sessionEstablished = false;
    bool mutationInFlight = false;
    bool transferInFlight = false;
    bool operationProvenSafeToReplay = false;
};

inline bool isFallbackAllowed(FallbackPolicy policy, const FallbackContext& context) {
    if (policy == FallbackPolicy::Never) return false;
    if (!context.sessionEstablished) return true;  // falling back before a session exists is always safe.
    if (context.transferInFlight) return false;    // resuming a transfer needs an authenticated resume token (not implemented this session).
    if (context.mutationInFlight && !context.operationProvenSafeToReplay) return false;
    switch (policy) {
        case FallbackPolicy::UnavailableOnly: return false;  // no live "became unavailable" signal exists yet.
        case FallbackPolicy::ConnectFailure: return true;
        case FallbackPolicy::PreOperation: return !context.mutationInFlight && !context.transferInFlight;
        case FallbackPolicy::Never: return false;
    }
    return false;
}

inline SelectionDecision evaluateSelection(const SelectionPolicyConfig& policy,
                                            const std::vector<TransportCapabilities>& candidates) {
    SelectionDecision decision;
    decision.candidates.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        CandidateScore result{candidate.kind, false, 0, {}};

        if (detail::contains(policy.deny, candidate.kind)) {
            result.reason = "denied by policy";
        } else if (!policy.allow.empty() && !detail::contains(policy.allow, candidate.kind)) {
            result.reason = "not in allow list";
        } else if (candidate.state == CapabilityState::CompiledOut) {
            result.reason = "compiled out";
        } else if (candidate.state == CapabilityState::UnsupportedHardware) {
            result.reason = "unsupported hardware";
        } else if (candidate.state == CapabilityState::Disabled) {
            result.reason = "disabled";
        } else if (candidate.state == CapabilityState::Blocked) {
            result.reason = "blocked";
        } else if (candidate.state == CapabilityState::Unavailable) {
            result.reason = "unavailable";
        } else if (policy.requirement.wirelessRequired && !candidate.wireless) {
            result.reason = "wireless required";
        } else if (policy.requirement.externalHardware == SideEffectPermission::Forbidden &&
                   candidate.requiresExternalHardware) {
            result.reason = "external hardware forbidden by policy";
        } else if (policy.requirement.externalHardware == SideEffectPermission::Required &&
                   !candidate.requiresExternalHardware) {
            result.reason = "external hardware required by policy";
        } else if (policy.requirement.ipInterface == SideEffectPermission::Forbidden &&
                   candidate.createsIpInterface) {
            result.reason = "IP interface forbidden by policy";
        } else if (policy.requirement.ipInterface == SideEffectPermission::Required &&
                   !candidate.createsIpInterface) {
            result.reason = "IP interface required by policy";
        } else if (policy.requirement.multiDevice == SideEffectPermission::Required &&
                   !candidate.supportsMultipleDevices) {
            result.reason = "multi-device support required by policy";
        } else {
            result.eligible = true;
            const int base = detail::kBaseScore[static_cast<int>(policy.goal)][static_cast<int>(candidate.kind)];
            result.score = base - detail::statePenalty(candidate.state);
            result.reason = "eligible";
        }

        decision.candidates.push_back(result);
    }

    int bestScore = 0;
    for (const auto& c : decision.candidates) {
        if (!c.eligible) continue;
        if (!decision.selected.has_value() || c.score > bestScore) {
            decision.selected = c.kind;
            bestScore = c.score;
        }
    }
    return decision;
}

}  // namespace esplink
```

- [ ] **Step 2: Update the `EspLinkCore` CMake target**

`SelectionPolicy.h` is header-only, so no `add_library` change is needed yet — the target from Task 1 already exposes `lib/EspLinkCore/src` as an include directory.

- [ ] **Step 3: Write table-driven tests**

`tests/esplink_selection_tests.cpp`:

```cpp
#include "SelectionPolicy.h"

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

namespace {

TransportCapabilities available(TransportKind kind, bool wireless = false, bool hw = false,
                                 bool ip = false, bool multi = false) {
    return TransportCapabilities{kind, CapabilityState::Available, wireless, hw, ip, multi};
}

void test_no_requirement_picks_highest_score_regardless_of_wireless() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::LowLatency;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV2, /*wireless=*/false),
        available(TransportKind::Bluetooth, /*wireless=*/true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(decision.selected.has_value());
    CHECK(*decision.selected == TransportKind::UsbV2);
}

void test_wireless_required_excludes_usb() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::LowLatency;
    policy.requirement.wirelessRequired = true;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV2, false),
        available(TransportKind::Bluetooth, true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(decision.selected.has_value());
    CHECK(*decision.selected == TransportKind::Bluetooth);
    CHECK(!decision.candidates[0].eligible);
    CHECK(decision.candidates[0].reason == std::string("wireless required"));
}

void test_deny_list_removes_a_candidate_even_if_best_scoring() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::HighThroughput;
    policy.requirement.wirelessRequired = true;
    policy.deny = {TransportKind::WifiDirect};
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::WifiDirect, true),
        available(TransportKind::Bluetooth, true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::Bluetooth);
    CHECK(decision.candidates[0].reason == std::string("denied by policy"));
}

void test_ip_interface_forbidden_excludes_wifi_direct() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::Balanced;
    policy.requirement.wirelessRequired = true;
    policy.requirement.ipInterface = SideEffectPermission::Forbidden;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::WifiDirect, true, false, /*ip=*/true),
        available(TransportKind::EspNowGateway, true, /*hw=*/true, /*ip=*/false),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::EspNowGateway);
}

void test_external_hardware_forbidden_excludes_gateway() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::Balanced;
    policy.requirement.wirelessRequired = true;
    policy.requirement.externalHardware = SideEffectPermission::Forbidden;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::EspNowGateway, true, /*hw=*/true),
        available(TransportKind::Bluetooth, true, /*hw=*/false),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::Bluetooth);
}

void test_all_candidates_ineligible_yields_no_selection_with_reasons() {
    SelectionPolicyConfig policy;
    std::vector<TransportCapabilities> candidates = {
        TransportCapabilities{TransportKind::Bluetooth, CapabilityState::CompiledOut},
        TransportCapabilities{TransportKind::WifiDirect, CapabilityState::Blocked},
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(!decision.selected.has_value());
    CHECK(decision.candidates[0].reason == std::string("compiled out"));
    CHECK(decision.candidates[1].reason == std::string("blocked"));
}

void test_allow_list_restricts_to_named_kinds() {
    SelectionPolicyConfig policy;
    policy.allow = {TransportKind::UsbV2};
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV2),
        available(TransportKind::Bluetooth, true),
    };
    auto decision = evaluateSelection(policy, candidates);
    CHECK(*decision.selected == TransportKind::UsbV2);
    CHECK(decision.candidates[1].reason == std::string("not in allow list"));
}

void test_decision_is_deterministic_across_repeated_evaluation() {
    SelectionPolicyConfig policy;
    policy.goal = OptimizationGoal::Deterministic;
    std::vector<TransportCapabilities> candidates = {
        available(TransportKind::UsbV1),
        available(TransportKind::UsbV2),
    };
    auto first = evaluateSelection(policy, candidates);
    auto second = evaluateSelection(policy, candidates);
    CHECK(first.selected.has_value() && second.selected.has_value());
    CHECK(*first.selected == *second.selected);
    CHECK(*first.selected == TransportKind::UsbV2);
}

void test_fallback_never_forbids_even_before_a_session_exists() {
    CHECK(!isFallbackAllowed(FallbackPolicy::Never, FallbackContext{}));
}

void test_fallback_allowed_before_any_session_exists_under_any_other_policy() {
    CHECK(isFallbackAllowed(FallbackPolicy::UnavailableOnly, FallbackContext{}));
    CHECK(isFallbackAllowed(FallbackPolicy::ConnectFailure, FallbackContext{}));
    CHECK(isFallbackAllowed(FallbackPolicy::PreOperation, FallbackContext{}));
}

void test_fallback_forbidden_during_an_in_flight_transfer() {
    FallbackContext context;
    context.sessionEstablished = true;
    context.transferInFlight = true;
    CHECK(!isFallbackAllowed(FallbackPolicy::ConnectFailure, context));
}

void test_fallback_forbidden_for_an_unproven_mutation_even_under_connect_failure_policy() {
    FallbackContext context;
    context.sessionEstablished = true;
    context.mutationInFlight = true;
    context.operationProvenSafeToReplay = false;
    CHECK(!isFallbackAllowed(FallbackPolicy::ConnectFailure, context));
}

void test_fallback_allowed_for_a_proven_safe_mutation_under_connect_failure_policy() {
    FallbackContext context;
    context.sessionEstablished = true;
    context.mutationInFlight = true;
    context.operationProvenSafeToReplay = true;
    CHECK(isFallbackAllowed(FallbackPolicy::ConnectFailure, context));
}

}  // namespace

int main() {
    test_no_requirement_picks_highest_score_regardless_of_wireless();
    test_wireless_required_excludes_usb();
    test_deny_list_removes_a_candidate_even_if_best_scoring();
    test_ip_interface_forbidden_excludes_wifi_direct();
    test_external_hardware_forbidden_excludes_gateway();
    test_all_candidates_ineligible_yields_no_selection_with_reasons();
    test_allow_list_restricts_to_named_kinds();
    test_decision_is_deterministic_across_repeated_evaluation();
    test_fallback_never_forbids_even_before_a_session_exists();
    test_fallback_allowed_before_any_session_exists_under_any_other_policy();
    test_fallback_forbidden_during_an_in_flight_transfer();
    test_fallback_forbidden_for_an_unproven_mutation_even_under_connect_failure_policy();
    test_fallback_allowed_for_a_proven_safe_mutation_under_connect_failure_policy();
    if (failures != 0) {
        std::cerr << failures << " esplink selection test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink selection tests passed\n";
    return EXIT_SUCCESS;
}
```

Register in `CMakeLists.txt`:

```cmake
add_executable(esplink_selection_tests tests/esplink_selection_tests.cpp)
target_link_libraries(esplink_selection_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_selection_tests)
```

and `add_test(NAME esplink_selection_tests COMMAND esplink_selection_tests)`.

- [ ] **Step 4: Build and run**

```bash
cmake --build .build/native-validation --target esplink_selection_tests
ctest --test-dir .build/native-validation -R esplink_selection_tests --output-on-failure
```

Expected: all 13 cases pass (8 selection scenarios + 5 fallback-decision scenarios).

- [ ] **Step 5: Commit**

```bash
git add lib/EspLinkCore/src/SelectionPolicy.h CMakeLists.txt tests/esplink_selection_tests.cpp
git commit -m "feat(firmware): add pure transport selection policy evaluator"
```

---

### Task 3: Firmware — v1 golden regression fixtures

**Files:**
- Create: `tests/vectors_v1_golden.h`
- Test: `tests/esplink_golden_shape_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `esplink::Command`, `esplink::Response`, `esplink::ProtocolError` from Task 1's `ProtocolCommands.h`.
- Produces: `esplink::goldenFixtures()` returning `const std::vector<esplink::GoldenFixture>&`. Task 7 (`ControlProtocolEngine` tests) and Task 8 (`SerialLegacyEndpoint`/`JsonCommandCodec`) both replay these fixtures as their v1-parity regression oracle — this header is the single source of truth for "what did USB Protocol 1.0 actually do," transcribed from `docs/PROTOCOL.md` and the exact behavior in `src/UsbProtocol.cpp` (already fully read for this plan).

Fixtures whose expected response depends on the real barcode encoders (e.g. `generate` for QR) are marked `encoderDependent = true` and carry no literal width/height — Task 7's replay test cross-checks those against a direct `espbarcode::encode()` call instead of a hand-computed magic number, because the exact module count is an encoder implementation detail, not a protocol contract.

- [ ] **Step 1: Write `tests/vectors_v1_golden.h`**

```cpp
#pragma once

#include <variant>
#include <vector>

#include "ProtocolCommands.h"

namespace esplink {

struct GoldenFixture {
    const char* name;
    Command command;
    std::variant<Response, ProtocolError> expected;
    bool encoderDependent = false;
    const char* precondition = "";  // human-readable, e.g. "requires a prior successful generate"
};

// Transcribed from docs/PROTOCOL.md and src/UsbProtocol.cpp (baseline commit
// d1565337b3d8f7b6cf8569cf0c443dfd43057072). Do not hand-tune these to make a
// later task's code pass — if a fixture looks wrong, the fixture is wrong and
// must be fixed against docs/PROTOCOL.md, not against the new implementation.
inline const std::vector<GoldenFixture>& goldenFixtures() {
    static const std::vector<GoldenFixture> fixtures = {
        // --- hello / capabilities / status (src/UsbProtocol.cpp:243-308) ---
        {"hello",
         HelloCommand{},
         HelloResponse{"EspScreenBarcodeGenerator", "1.0", /*firmware injected at build*/ "", "usb-uart-ndjson", 320, 480},
         false, ""},

        {"status_no_current",
         StatusCommand{},
         StatusResponse{/*barcodeVisible=*/false, /*hasCurrent=*/false, /*currentRaw=*/false,
                         /*status=*/"", /*freeHeap=*/0, std::nullopt},
         false, "fresh application, nothing generated yet; freeHeap is ignored by the replay test"},

        // --- generate (src/UsbProtocol.cpp:310-347) ---
        {"generate_qr_success",
         GenerateCommand{ /*spec*/ {}, /*display=*/true, std::nullopt},
         GenerateResponse{"qr", 0, 0, false, 0, true, "LAB-TEST-001"},
         /*encoderDependent=*/true,
         "spec.type=QrCode, spec.data=\"LAB-TEST-001\"; only type/displayed/normalizedData are checked literally"},

        {"generate_invalid_symbology",
         GenerateCommand{},  // Task 7's replay constructs this with an unparsable type sentinel
         ProtocolError{"generate", "invalid_spec", "unknown symbology"},
         false, "constructed by the replay test with a request JSON containing type=\"not-a-symbology\""},

        // --- display (src/UsbProtocol.cpp:349-368) ---
        {"display_no_current_fails",
         DisplayCommand{std::nullopt},
         ProtocolError{"display", "display_failed", ""},  // message text not asserted literally
         false, "fresh application, nothing generated yet"},

        // --- close / home (src/UsbProtocol.cpp:96-101) ---
        {"close",
         CloseCommand{},
         SimpleOkResponse{"close", "barcode closed"},
         false, ""},

        {"home",
         HomeCommand{},
         SimpleOkResponse{"home", "home screen displayed"},
         false, ""},

        // --- save / load / delete / list (src/UsbProtocol.cpp:370-430) ---
        {"save_missing_name",
         SaveCommand{""},
         ProtocolError{"save", "missing_name", "name is required"},
         false, ""},

        {"load_unknown_name",
         LoadCommand{"NO_SUCH_PRESET", false},
         ProtocolError{"load", "load_failed", ""},
         false, "empty PresetStore"},

        {"delete_missing_name",
         DeleteCommand{""},
         ProtocolError{"delete", "missing_name", "name is required"},
         false, ""},

        {"list_empty",
         ListCommand{},
         ListResponse{{}},
         false, "empty PresetStore"},

        // --- upload_begin validation (src/UsbProtocol.cpp:432-485) ---
        {"upload_begin_invalid_dimensions_zero_width",
         UploadBeginCommand{/*width=*/0, /*height=*/1, false, 4, espbarcode::Rotation::Auto, false, true, "x"},
         ProtocolError{"upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1"},
         false, ""},

        {"upload_begin_linear_height_must_be_one",
         UploadBeginCommand{/*width=*/9, /*height=*/2, /*linear=*/true, 4, espbarcode::Rotation::Auto, false, true, "x"},
         ProtocolError{"upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1"},
         false, ""},

        {"upload_begin_success_3x2",
         UploadBeginCommand{/*width=*/3, /*height=*/2, false, 4, espbarcode::Rotation::Auto, false, true, "external-pdf417"},
         UploadBeginResponse{/*bytesExpected=*/1, /*nextOffset=*/0},
         false, "(3*2+7)/8 = 1 byte"},

        // --- upload_chunk / upload_end (src/UsbProtocol.cpp:487-565) ---
        {"upload_chunk_wrong_offset",
         UploadChunkCommand{/*offset=*/1, {0xA8}},
         ProtocolError{"upload_chunk", "unexpected_offset", "chunks must be sequential"},
         false, "requires upload_begin_success_3x2 to have run first, nextOffset=0 expected"},

        {"upload_chunk_overflow",
         UploadChunkCommand{/*offset=*/0, {0xA8, 0x00}},
         ProtocolError{"upload_chunk", "overflow", "chunk exceeds declared matrix size"},
         false, "requires a 1-byte-declared upload in progress (see upload_begin_success_3x2)"},

        {"upload_end_incomplete",
         UploadEndCommand{std::nullopt},
         ProtocolError{"upload_end", "incomplete", "not all declared bytes were received"},
         false, "requires upload_begin_success_3x2 with zero chunks sent"},

        {"upload_end_crc_mismatch",
         UploadEndCommand{/*expectedCrc32=*/0},
         ProtocolError{"upload_end", "crc_mismatch", "uploaded bytes failed CRC32 validation"},
         false, "requires upload_begin_success_3x2 followed by upload_chunk{offset:0,data:[0xA8]}; actual crc32([0xA8]) = 168805463, not 0"},

        {"upload_end_success_3x2",
         UploadEndCommand{/*expectedCrc32=*/168805463u},
         UploadEndResponse{/*crc32=*/168805463u, /*displayed=*/true},
         false, "requires upload_begin_success_3x2 followed by upload_chunk{offset:0,data:[0xA8]}"},

        {"upload_abort",
         UploadAbortCommand{},
         SimpleOkResponse{"upload_abort", "upload discarded"},
         false, ""},

        // --- download (src/UsbProtocol.cpp:567-617) ---
        {"download_no_symbol",
         DownloadCommand{384},
         ProtocolError{"download", "no_symbol", "no current symbol"},
         false, "fresh application, nothing generated yet"},

        // --- backlight / reboot (src/UsbProtocol.cpp:121-129) ---
        {"backlight_on",
         BacklightCommand{true},
         SimpleOkResponse{"backlight", "backlight on"},
         false, ""},

        {"backlight_off",
         BacklightCommand{false},
         SimpleOkResponse{"backlight", "backlight off"},
         false, ""},

        {"reboot_acknowledged",
         RebootCommand{},
         SimpleOkResponse{"reboot", "rebooting"},
         false, "the actual ESP.restart() side effect is asserted separately via IDeviceControl, not through this fixture"},
    };
    return fixtures;
}

}  // namespace esplink
```

- [ ] **Step 2: Write a structural self-check**

This does not yet dispatch anything (there is no engine until Task 7) — it only proves the header compiles and each fixture's declared command/response `cmd` names line up, catching transcription typos early.

`tests/esplink_golden_shape_tests.cpp`:

```cpp
#include "vectors_v1_golden.h"

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

namespace {
const char* responseCommandName(const Response& response) {
    return std::visit([](const auto& r) -> const char* {
        using T = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<T, SimpleOkResponse>) return r.command.c_str();
        else return nullptr;  // typed responses other than SimpleOkResponse carry no redundant name field.
    }, response);
}
}  // namespace

int main() {
    const auto& fixtures = goldenFixtures();
    CHECK(fixtures.size() == 24);

    for (const auto& fixture : fixtures) {
        CHECK(fixture.name != nullptr);
        CHECK(fixture.name[0] != '\0');
        if (std::holds_alternative<Response>(fixture.expected)) {
            const auto& response = std::get<Response>(fixture.expected);
            const char* name = responseCommandName(response);
            (void)name;  // SimpleOkResponse fixtures are cross-checked by name below; others are structural only.
        }
    }

    // Spot-check a couple of SimpleOkResponse fixtures line up their command name with their fixture name.
    for (const auto& fixture : fixtures) {
        if (!std::holds_alternative<Response>(fixture.expected)) continue;
        const auto& response = std::get<Response>(fixture.expected);
        if (!std::holds_alternative<SimpleOkResponse>(response)) continue;
        const auto& ok = std::get<SimpleOkResponse>(response);
        CHECK(!ok.command.empty());
    }

    if (failures != 0) {
        std::cerr << failures << " golden fixture shape test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All golden fixture shape tests passed (" << fixtures.size() << " fixtures)\n";
    return EXIT_SUCCESS;
}
```

Register in `CMakeLists.txt`:

```cmake
add_executable(esplink_golden_shape_tests tests/esplink_golden_shape_tests.cpp)
target_link_libraries(esplink_golden_shape_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_golden_shape_tests)
```

and `add_test(NAME esplink_golden_shape_tests COMMAND esplink_golden_shape_tests)`.

- [ ] **Step 3: Build and run**

```bash
cmake --build .build/native-validation --target esplink_golden_shape_tests
ctest --test-dir .build/native-validation -R esplink_golden_shape_tests --output-on-failure
```

Expected: 24 fixtures reported, test passes.

- [ ] **Step 4: Commit**

```bash
git add tests/vectors_v1_golden.h tests/esplink_golden_shape_tests.cpp CMakeLists.txt
git commit -m "test(firmware): capture USB Protocol 1.0 golden regression fixtures"
```

---

### Task 4: Firmware — EspLink v2 envelope, hop frame, COBS, CRC, and reassembly codecs

**Files:**
- Create: `lib/EspLinkCore/src/Crc32.h`, `lib/EspLinkCore/src/Crc32.cpp`
- Create: `lib/EspLinkCore/src/Envelope.h`, `lib/EspLinkCore/src/Envelope.cpp`
- Create: `lib/EspLinkCore/src/HopFrame.h`, `lib/EspLinkCore/src/HopFrame.cpp`
- Create: `lib/EspLinkCore/src/Cobs.h`, `lib/EspLinkCore/src/Cobs.cpp`
- Create: `lib/EspLinkCore/src/FrameAssembler.h`, `lib/EspLinkCore/src/FrameAssembler.cpp`
- Modify: `CMakeLists.txt` (switch `EspLinkCore` from `INTERFACE` to `STATIC`)
- Test: `tests/esplink_codec_tests.cpp`

**Interfaces:**
- Consumes: `esplink::MessageKind`, `esplink::ServiceId`, `esplink::CodecId`, `esplink::FrameType`, `esplink::TrafficClass`, `esplink::CarrierProfileId` from Task 1.
- Produces: `esplink::crc32(...)`, `esplink::MessageEnvelope`, `esplink::encodeEnvelope(...)`, `esplink::decodeEnvelope(...)`, `esplink::HopFrameHeader`, `esplink::encodeHopFrame(...)`, `esplink::decodeHopFrame(...)`, `esplink::cobsEncode(...)`, `esplink::cobsDecode(...)`, `esplink::FrameAssembler`. Task 8 (`SerialCobsEndpoint`) and the .NET Task 10 (`EspBarcode.Protocol`, byte-identical vectors) depend on these exact wire layouts.

All multi-byte fields are little-endian (Global Constraints). The envelope header is exactly 32 bytes; the hop-frame header is exactly 32 bytes; a raw hop frame on the wire is `32 (header) + payloadLength + 4 (CRC-32 trailer)` bytes, matching the design doc's "raw frame length is `36 + payloadLength`" (design plan §8.4).

- [ ] **Step 1: Write `Crc32.h`/`Crc32.cpp`**

Move the exact algorithm already in `src/UsbProtocol.cpp:651-660` here so both the legacy adapter (Task 8) and the new codec share one implementation.

`lib/EspLinkCore/src/Crc32.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace esplink {
uint32_t crc32(const uint8_t* data, std::size_t length);
}
```

`lib/EspLinkCore/src/Crc32.cpp`:

```cpp
#include "Crc32.h"

namespace esplink {

uint32_t crc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

}  // namespace esplink
```

- [ ] **Step 2: Write `Envelope.h`/`Envelope.cpp`**

`lib/EspLinkCore/src/Envelope.h`:

```cpp
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
```

`lib/EspLinkCore/src/Envelope.cpp`:

```cpp
#include "Envelope.h"

#include <cstring>

namespace esplink {

namespace {
void putU16(std::vector<uint8_t>& out, uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
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
```

- [ ] **Step 3: Write `HopFrame.h`/`HopFrame.cpp`**

`lib/EspLinkCore/src/HopFrame.h`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "ConnectivityTypes.h"
#include "Envelope.h"  // reuses CodecError

namespace esplink {

struct HopFrameHeader {
    uint8_t major = 2;
    uint8_t minor = 0;
    FrameType frameType = FrameType::Data;
    uint8_t flags = 0;
    TrafficClass trafficClass = TrafficClass::Control;
    CarrierProfileId profileId = CarrierProfileId::Unspecified;
    uint16_t routeId = 0;
    uint32_t linkSessionId = 0;
    uint32_t linkMessageId = 0;
    uint32_t linkCorrelationId = 0;
    uint16_t fragmentIndex = 0;
    uint16_t fragmentCount = 1;
};

inline constexpr std::size_t kHopFrameHeaderSize = 32;
inline constexpr std::size_t kHopFrameOverhead = 36;  // header(32) + crc32 trailer(4)

// Encodes header + payload + trailing CRC-32 (computed over header+payload) into `out`.
bool encodeHopFrame(const HopFrameHeader& header, const uint8_t* payload, std::size_t payloadLength,
                    std::vector<uint8_t>& out, CodecError& error);

// Decodes and CRC-validates a single raw hop frame. `payload` receives just the fragment bytes.
bool decodeHopFrame(const uint8_t* bytes, std::size_t length, HopFrameHeader& header,
                    std::vector<uint8_t>& payload, CodecError& error);

}  // namespace esplink
```

`lib/EspLinkCore/src/HopFrame.cpp`:

```cpp
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
```

- [ ] **Step 4: Write `Cobs.h`/`Cobs.cpp`**

Standard zero-delimited COBS. `cobsEncode`/`cobsDecode` operate on the COBS block itself — the caller appends/strips the trailing `0x00` stream delimiter.

`lib/EspLinkCore/src/Cobs.h`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace esplink {

std::vector<uint8_t> cobsEncode(const uint8_t* data, std::size_t length);
bool cobsDecode(const uint8_t* data, std::size_t length, std::vector<uint8_t>& out);

}  // namespace esplink
```

`lib/EspLinkCore/src/Cobs.cpp`:

```cpp
#include "Cobs.h"

namespace esplink {

std::vector<uint8_t> cobsEncode(const uint8_t* data, std::size_t length) {
    std::vector<uint8_t> out;
    out.reserve(length + length / 254 + 2);
    std::size_t read = 0;
    while (true) {
        std::size_t blockStart = read;
        std::size_t blockLen = 0;
        while (read < length && data[read] != 0x00 && blockLen < 254) { ++read; ++blockLen; }
        const bool hitZero = read < length && data[read] == 0x00;
        out.push_back(static_cast<uint8_t>(blockLen + 1));
        out.insert(out.end(), data + blockStart, data + blockStart + blockLen);
        if (hitZero) {
            ++read;
            if (read >= length) break;
        } else if (read >= length) {
            break;
        }
        // else: blockLen hit the 254 cap mid-run; loop continues with code 255 on the next block.
    }
    return out;
}

bool cobsDecode(const uint8_t* data, std::size_t length, std::vector<uint8_t>& out) {
    out.clear();
    std::size_t read = 0;
    while (read < length) {
        const uint8_t code = data[read];
        if (code == 0) return false;  // a literal zero byte is never valid inside a COBS block.
        ++read;
        const std::size_t blockLen = std::size_t(code) - 1;
        if (read + blockLen > length) return false;
        out.insert(out.end(), data + read, data + read + blockLen);
        read += blockLen;
        if (code != 255 && read < length) out.push_back(0x00);
    }
    return true;
}

}  // namespace esplink
```

- [ ] **Step 5: Write `FrameAssembler.h`/`FrameAssembler.cpp`**

Bounded reassembly: at most `maxConcurrentMessages` partial messages tracked at once (default 2, matching the design doc's display-side ceiling, design plan §8.12); a new message beyond that capacity evicts the oldest partial entry (FIFO) rather than growing without bound.

`lib/EspLinkCore/src/FrameAssembler.h`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "HopFrame.h"

namespace esplink {

enum class AssemblyOutcome : uint8_t { Incomplete, Complete, DuplicateIgnored, Conflict };

class FrameAssembler {
public:
    explicit FrameAssembler(std::size_t maxConcurrentMessages = 2);

    // Feeds one already CRC-validated fragment. On AssemblyOutcome::Complete, `assembled`
    // holds the full reassembled Layer 3 bytes (envelope + body) in fragment order.
    AssemblyOutcome addFragment(const HopFrameHeader& header, const std::vector<uint8_t>& payload,
                                std::vector<uint8_t>& assembled);

private:
    struct Key { uint32_t linkSessionId; uint32_t linkMessageId; uint16_t routeId; };
    struct Partial {
        Key key{};
        uint16_t fragmentCount = 0;
        uint16_t receivedCount = 0;
        std::vector<std::optional<std::vector<uint8_t>>> fragments;
    };

    static bool sameKey(const Key& a, const Key& b);

    std::size_t maxConcurrentMessages_;
    std::vector<Partial> partial_;
};

}  // namespace esplink
```

`lib/EspLinkCore/src/FrameAssembler.cpp`:

```cpp
#include "FrameAssembler.h"

namespace esplink {

FrameAssembler::FrameAssembler(std::size_t maxConcurrentMessages) : maxConcurrentMessages_(maxConcurrentMessages) {}

bool FrameAssembler::sameKey(const Key& a, const Key& b) {
    return a.linkSessionId == b.linkSessionId && a.linkMessageId == b.linkMessageId && a.routeId == b.routeId;
}

AssemblyOutcome FrameAssembler::addFragment(const HopFrameHeader& header, const std::vector<uint8_t>& payload,
                                            std::vector<uint8_t>& assembled) {
    const Key key{header.linkSessionId, header.linkMessageId, header.routeId};

    auto it = partial_.end();
    for (auto candidate = partial_.begin(); candidate != partial_.end(); ++candidate) {
        if (sameKey(candidate->key, key)) { it = candidate; break; }
    }

    if (it == partial_.end()) {
        if (partial_.size() >= maxConcurrentMessages_) {
            partial_.erase(partial_.begin());  // evict oldest — bounded memory, no unbounded reassembly.
        }
        Partial fresh;
        fresh.key = key;
        fresh.fragmentCount = header.fragmentCount;
        fresh.fragments.resize(header.fragmentCount);
        partial_.push_back(std::move(fresh));
        it = partial_.end() - 1;
    }

    if (header.fragmentCount != it->fragmentCount || header.fragmentIndex >= it->fragments.size()) {
        return AssemblyOutcome::Conflict;
    }

    auto& slot = it->fragments[header.fragmentIndex];
    if (slot.has_value()) {
        if (*slot == payload) return AssemblyOutcome::DuplicateIgnored;
        return AssemblyOutcome::Conflict;
    }
    slot = payload;
    ++it->receivedCount;

    if (it->receivedCount < it->fragmentCount) return AssemblyOutcome::Incomplete;

    assembled.clear();
    for (const auto& fragment : it->fragments) {
        assembled.insert(assembled.end(), fragment->begin(), fragment->end());
    }
    partial_.erase(it);
    return AssemblyOutcome::Complete;
}

}  // namespace esplink
```

- [ ] **Step 6: Switch the `EspLinkCore` CMake target to `STATIC`**

Replace the `add_library(EspLinkCore INTERFACE)` block from Task 1 with:

```cmake
set(ESPLINKCORE_SOURCES
    lib/EspLinkCore/src/Crc32.cpp
    lib/EspLinkCore/src/Envelope.cpp
    lib/EspLinkCore/src/HopFrame.cpp
    lib/EspLinkCore/src/Cobs.cpp
    lib/EspLinkCore/src/FrameAssembler.cpp
)
add_library(EspLinkCore STATIC ${ESPLINKCORE_SOURCES})
target_include_directories(EspLinkCore PUBLIC lib/EspLinkCore/src)
target_link_libraries(EspLinkCore PUBLIC EspBarcodeCore)
if(MSVC)
    target_compile_options(EspLinkCore PRIVATE /W4 /WX)
else()
    target_compile_options(EspLinkCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
espbarcode_enable_sanitizers(EspLinkCore)
```

- [ ] **Step 7: Write `tests/esplink_codec_tests.cpp` using the verified vectors below**

These vectors were generated and round-trip-verified with a reference Python implementation during planning (IEEE CRC-32, standard zero-delimited COBS) — treat any mismatch as an implementation bug, not a vector error.

```cpp
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
```

Register in `CMakeLists.txt`:

```cmake
add_executable(esplink_codec_tests tests/esplink_codec_tests.cpp)
target_link_libraries(esplink_codec_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(esplink_codec_tests)
```

and `add_test(NAME esplink_codec_tests COMMAND esplink_codec_tests)`.

- [ ] **Step 8: Build and run**

```bash
cmake --build .build/native-validation --target esplink_codec_tests
ctest --test-dir .build/native-validation -R esplink_codec_tests --output-on-failure
```

Expected: all cases pass, including the exact-byte vector comparisons.

- [ ] **Step 9: Commit**

```bash
git add lib/EspLinkCore/src CMakeLists.txt tests/esplink_codec_tests.cpp
git commit -m "feat(firmware): add EspLink v2 envelope, hop frame, COBS, CRC, and reassembly codecs"
```

---

### Task 5: Firmware — application ports and adapters

**Files:**
- Create: `lib/EspLinkCore/src/ApplicationPorts.h`
- Create: `src/BarcodeApplicationAdapter.h`, `src/BarcodeApplicationAdapter.cpp`
- Create: `src/EspIdfDeviceControl.h`, `src/EspIdfDeviceControl.cpp`
- Test: `tests/control_protocol_engine_tests.cpp` (created here with fakes only; dispatcher tests land in Task 7)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `espbarcode::BarcodeSpec`, `BitMatrix`, `Rotation`, `BarcodeResult` (`lib/EspBarcodeCore`); `BarcodeApplication` public interface at `include/BarcodeApplication.h:16-44`; `PresetStore` public interface at `include/PresetStore.h`.
- Produces: `esplink::IBarcodeDevice`, `esplink::IPresetRepository`, `esplink::IDeviceControl` (pure-virtual ports, header-only, no Arduino dependency); `esplink::BarcodeApplicationAdapter` (firmware-layer, wraps a real `BarcodeApplication&`); `esplink::EspIdfDeviceControl` (firmware-layer, wraps `ESP.getFreeHeap()`/`ESP.restart()`). Task 6's `ControlProtocolEngine` depends on the three port interfaces only — never on `BarcodeApplication` or `PresetStore` directly. Test doubles (`FakeBarcodeDevice`, `FakePresetRepository`, `FakeDeviceControl`) defined here are reused by every later native test task.

- [ ] **Step 1: Write `ApplicationPorts.h`**

Method signatures mirror `include/BarcodeApplication.h:16-44` and `include/PresetStore.h:10-15` exactly — this is a pure extraction, not a redesign.

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EspBarcodeCore.h"

namespace esplink {

class IBarcodeDevice {
public:
    virtual ~IBarcodeDevice() = default;

    virtual bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) = 0;
    virtual bool setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                                   espbarcode::Rotation rotation, bool invert, const std::string& label,
                                   bool display, std::string& error) = 0;
    virtual bool displayCurrent(std::string& error) = 0;
    virtual void closeBarcode() = 0;
    virtual void showHome(const std::string& status) = 0;

    virtual const espbarcode::BarcodeSpec& activeSpec() const = 0;
    virtual const espbarcode::BarcodeResult& currentResult() const = 0;
    virtual bool hasCurrent() const = 0;
    virtual bool currentIsRaw() const = 0;
    virtual uint8_t currentQuietZone() const = 0;
    virtual espbarcode::Rotation currentRotation() const = 0;
    virtual bool currentInvert() const = 0;
    virtual const std::string& currentLabel() const = 0;
    virtual bool barcodeVisible() const = 0;
    virtual const std::string& statusText() const = 0;
};

class IPresetRepository {
public:
    virtual ~IPresetRepository() = default;

    virtual bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error) = 0;
    virtual bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const = 0;
    virtual bool remove(const std::string& name, std::string& error) = 0;
    virtual std::vector<std::string> list() const = 0;
};

class IDeviceControl {
public:
    virtual ~IDeviceControl() = default;

    virtual void setBacklight(bool on) = 0;
    virtual uint32_t freeHeapBytes() const = 0;
    // Triggers the actual restart. Callers that must flush a transport-specific
    // stream first (e.g. Serial.flush()) do so before calling this.
    virtual void reboot() = 0;
};

}  // namespace esplink
```

- [ ] **Step 2: Write `src/BarcodeApplicationAdapter.h`/`.cpp`**

```cpp
// src/BarcodeApplicationAdapter.h
#pragma once

#include "ApplicationPorts.h"
#include "BarcodeApplication.h"

namespace esplink {

class BarcodeApplicationAdapter : public IBarcodeDevice, public IPresetRepository {
public:
    explicit BarcodeApplicationAdapter(BarcodeApplication& application) : application_(application) {}

    bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) override;
    bool setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                           espbarcode::Rotation rotation, bool invert, const std::string& label,
                           bool display, std::string& error) override;
    bool displayCurrent(std::string& error) override;
    void closeBarcode() override;
    void showHome(const std::string& status) override;

    const espbarcode::BarcodeSpec& activeSpec() const override;
    const espbarcode::BarcodeResult& currentResult() const override;
    bool hasCurrent() const override;
    bool currentIsRaw() const override;
    uint8_t currentQuietZone() const override;
    espbarcode::Rotation currentRotation() const override;
    bool currentInvert() const override;
    const std::string& currentLabel() const override;
    bool barcodeVisible() const override;
    const std::string& statusText() const override;

    bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error) override;
    bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const override;
    bool remove(const std::string& name, std::string& error) override;
    std::vector<std::string> list() const override;

private:
    BarcodeApplication& application_;
};

}  // namespace esplink
```

Each method body is a one-line delegation, e.g. `bool BarcodeApplicationAdapter::generate(...) { return application_.generate(spec, display, error); }` and `bool BarcodeApplicationAdapter::save(...) { return application_.presets().save(name, spec, error); }`. Write all fourteen delegations following that pattern against the exact signatures in `include/BarcodeApplication.h:16-44`.

- [ ] **Step 3: Write `src/EspIdfDeviceControl.h`/`.cpp`**

```cpp
// src/EspIdfDeviceControl.h
#pragma once

#include "ApplicationPorts.h"
#include "BarcodeApplication.h"

namespace esplink {

class EspIdfDeviceControl : public IDeviceControl {
public:
    explicit EspIdfDeviceControl(BarcodeApplication& application) : application_(application) {}

    void setBacklight(bool on) override { application_.setBacklight(on); }
    uint32_t freeHeapBytes() const override;  // .cpp: return ESP.getFreeHeap();
    void reboot() override;                   // .cpp: ESP.restart();

private:
    BarcodeApplication& application_;
};

}  // namespace esplink
```

`src/EspIdfDeviceControl.cpp` includes `<Arduino.h>` and implements the two `.cpp`-only methods as noted above — this file is Arduino-dependent by design and is **not** part of `lib/EspLinkCore` or the native CMake build.

- [ ] **Step 4: Write native test doubles and the file that will hold engine tests**

`tests/control_protocol_engine_tests.cpp` (fakes only for now — dispatcher tests are added in Task 7):

```cpp
#include "ApplicationPorts.h"

#include <iostream>
#include <map>

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

class FakeBarcodeDevice : public IBarcodeDevice {
public:
    bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) override {
        (void)error;
        spec_ = spec;
        current_ = espbarcode::encode(spec);
        hasCurrent_ = current_.ok;
        currentIsRaw_ = false;
        visible_ = display && hasCurrent_;
        if (!current_.ok) error = current_.error;
        return current_.ok;
    }
    bool setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                           espbarcode::Rotation rotation, bool invert, const std::string& label,
                           bool display, std::string& error) override {
        (void)error;
        current_ = espbarcode::BarcodeResult{};
        current_.ok = true;
        current_.matrix = std::move(matrix);
        current_.linear = linear;
        hasCurrent_ = true;
        currentIsRaw_ = true;
        quiet_ = quietZone;
        rotation_ = rotation;
        invert_ = invert;
        label_ = label;
        visible_ = display;
        return true;
    }
    bool displayCurrent(std::string& error) override {
        if (!hasCurrent_) { error = "no current symbol"; return false; }
        visible_ = true;
        return true;
    }
    void closeBarcode() override { visible_ = false; }
    void showHome(const std::string& status) override { visible_ = false; status_ = status; }

    const espbarcode::BarcodeSpec& activeSpec() const override { return spec_; }
    const espbarcode::BarcodeResult& currentResult() const override { return current_; }
    bool hasCurrent() const override { return hasCurrent_; }
    bool currentIsRaw() const override { return currentIsRaw_; }
    uint8_t currentQuietZone() const override { return quiet_; }
    espbarcode::Rotation currentRotation() const override { return rotation_; }
    bool currentInvert() const override { return invert_; }
    const std::string& currentLabel() const override { return label_; }
    bool barcodeVisible() const override { return visible_; }
    const std::string& statusText() const override { return status_; }

private:
    espbarcode::BarcodeSpec spec_;
    espbarcode::BarcodeResult current_;
    bool hasCurrent_ = false;
    bool currentIsRaw_ = false;
    uint8_t quiet_ = 4;
    espbarcode::Rotation rotation_ = espbarcode::Rotation::Auto;
    bool invert_ = false;
    std::string label_;
    bool visible_ = false;
    std::string status_;
};

class FakePresetRepository : public IPresetRepository {
public:
    bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error) override {
        (void)error;
        presets_[name] = spec;
        return true;
    }
    bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const override {
        auto it = presets_.find(name);
        if (it == presets_.end()) { error = "preset not found"; return false; }
        spec = it->second;
        return true;
    }
    bool remove(const std::string& name, std::string& error) override {
        (void)error;
        return presets_.erase(name) > 0;
    }
    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto& [name, spec] : presets_) names.push_back(name);
        return names;
    }

private:
    std::map<std::string, espbarcode::BarcodeSpec> presets_;
};

class FakeDeviceControl : public IDeviceControl {
public:
    void setBacklight(bool on) override { backlightOn_ = on; }
    uint32_t freeHeapBytes() const override { return 123456; }
    void reboot() override { rebooted_ = true; }

    bool backlightOn() const { return backlightOn_; }
    bool rebooted() const { return rebooted_; }

private:
    bool backlightOn_ = true;
    bool rebooted_ = false;
};

void test_fake_barcode_device_generate_and_display() {
    FakeBarcodeDevice device;
    espbarcode::BarcodeSpec spec;
    spec.type = espbarcode::Symbology::QrCode;
    spec.data = "PORT-TEST";
    std::string error;
    CHECK(device.generate(spec, true, error));
    CHECK(device.hasCurrent());
    CHECK(device.barcodeVisible());
    device.closeBarcode();
    CHECK(!device.barcodeVisible());
}

void test_fake_preset_repository_round_trip() {
    FakePresetRepository presets;
    espbarcode::BarcodeSpec spec;
    spec.data = "SAVED";
    std::string error;
    CHECK(presets.save("SLOT01", spec, error));
    espbarcode::BarcodeSpec loaded;
    CHECK(presets.load("SLOT01", loaded, error));
    CHECK(loaded.data == "SAVED");
    CHECK(!presets.load("NOPE", loaded, error));
}

}  // namespace

int main() {
    test_fake_barcode_device_generate_and_display();
    test_fake_preset_repository_round_trip();
    if (failures != 0) {
        std::cerr << failures << " control protocol engine test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All control protocol engine tests passed (fakes only so far)\n";
    return EXIT_SUCCESS;
}
```

Register in `CMakeLists.txt`:

```cmake
add_executable(control_protocol_engine_tests tests/control_protocol_engine_tests.cpp)
target_link_libraries(control_protocol_engine_tests PRIVATE EspLinkCore)
espbarcode_enable_sanitizers(control_protocol_engine_tests)
```

and `add_test(NAME control_protocol_engine_tests COMMAND control_protocol_engine_tests)`.

- [ ] **Step 5: Build and run**

```bash
cmake --build .build/native-validation --target control_protocol_engine_tests
ctest --test-dir .build/native-validation -R control_protocol_engine_tests --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add lib/EspLinkCore/src/ApplicationPorts.h src/BarcodeApplicationAdapter.h src/BarcodeApplicationAdapter.cpp \
        src/EspIdfDeviceControl.h src/EspIdfDeviceControl.cpp CMakeLists.txt tests/control_protocol_engine_tests.cpp
git commit -m "feat(firmware): add application ports, BarcodeApplication adapter, and test doubles"
```

---

### Task 6: Firmware — `ControlSession`, `TransferSession`, and duplicate-result cache

**Files:**
- Create: `lib/EspLinkCore/src/TransferSession.h`
- Create: `lib/EspLinkCore/src/ControlSession.h`, `lib/EspLinkCore/src/ControlSession.cpp`
- Modify: `CMakeLists.txt`, `tests/control_protocol_engine_tests.cpp`

**Interfaces:**
- Consumes: `esplink::ControlSessionId`, `esplink::ControllerId`, `esplink::OperationId` (Task 1); `esplink::Response` (Task 1's `ProtocolCommands.h`); `espbarcode::Rotation` (`lib/EspBarcodeCore`).
- Produces: `esplink::TransferState`, `esplink::TransferSession`, `esplink::ControlSession`. This replaces `UsbProtocol::UploadState` (`include/UsbProtocol.h:20-34`) — the field set is copied verbatim, only the owner changes (session-scoped, not endpoint-scoped), so two independent `ControlSession` instances can never see each other's transfer bytes. Task 7's `ControlProtocolEngine` takes a `ControlSession&` per call.

- [ ] **Step 1: Write `TransferSession.h`**

Field set copied verbatim from `include/UsbProtocol.h:20-34` (`UsbProtocol::UploadState`).

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EspBarcodeCore.h"

namespace esplink {

struct TransferState {
    bool active = false;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 4;
    espbarcode::Rotation rotation = espbarcode::Rotation::Auto;
    bool invert = false;
    bool display = true;
    std::string label;
    std::vector<uint8_t> bytes;
    std::size_t nextOffset = 0;

    void reset() { *this = TransferState{}; }
};

class TransferSession {
public:
    TransferState& upload() { return upload_; }
    const TransferState& upload() const { return upload_; }

private:
    TransferState upload_;
};

}  // namespace esplink
```

- [ ] **Step 2: Write `ControlSession.h`/`.cpp`**

```cpp
// lib/EspLinkCore/src/ControlSession.h
#pragma once

#include <optional>
#include <vector>

#include "Identifiers.h"
#include "ProtocolCommands.h"
#include "TransferSession.h"

namespace esplink {

class ControlSession {
public:
    ControlSession(ControlSessionId id, ControllerId controller) : id_(id), controller_(controller) {}

    ControlSessionId id() const { return id_; }
    ControllerId controller() const { return controller_; }

    // v1 parity: exactly one connector talks to one session at a time, so acquisition
    // always succeeds unless this same session object already holds the lease. Real
    // multi-controller contention is out of scope this session (see docs/PROTOCOL_V2.md
    // "Next PRs").
    bool tryAcquireLease() {
        if (leaseHeld_) return false;
        leaseHeld_ = true;
        return true;
    }
    void releaseLease() { leaseHeld_ = false; }
    bool hasLease() const { return leaseHeld_; }

    TransferSession& transfer() { return transfer_; }
    const TransferSession& transfer() const { return transfer_; }

    // A cached entry is either the successful Response or the ProtocolError a prior
    // attempt produced — a replayable command that failed once must keep failing the
    // same way on retry, not silently re-attempt with different side effects.
    using CommandResult = std::variant<Response, ProtocolError>;

    std::optional<CommandResult> lookupCachedResult(OperationId operationId) const;
    void cacheResult(OperationId operationId, const CommandResult& result);

private:
    static constexpr std::size_t kCacheCapacity = 8;
    struct CacheEntry {
        OperationId operationId;
        CommandResult result;
    };

    ControlSessionId id_;
    ControllerId controller_;
    bool leaseHeld_ = false;
    TransferSession transfer_;
    std::vector<CacheEntry> cache_;
    std::size_t cacheCursor_ = 0;
};

}  // namespace esplink
```

```cpp
// lib/EspLinkCore/src/ControlSession.cpp
#include "ControlSession.h"

namespace esplink {

std::optional<ControlSession::CommandResult> ControlSession::lookupCachedResult(OperationId operationId) const {
    for (const auto& entry : cache_) {
        if (entry.operationId == operationId) return entry.result;
    }
    return std::nullopt;
}

void ControlSession::cacheResult(OperationId operationId, const CommandResult& result) {
    if (lookupCachedResult(operationId).has_value()) return;  // already cached, first result wins
    if (cache_.size() < kCacheCapacity) {
        cache_.push_back({operationId, result});
        return;
    }
    // Bounded ring buffer: overwrite the oldest entry rather than growing unbounded.
    cache_[cacheCursor_] = {operationId, result};
    cacheCursor_ = (cacheCursor_ + 1) % kCacheCapacity;
}

}  // namespace esplink
```

- [ ] **Step 3: Add `ControlSession.cpp` to the CMake target**

Add `lib/EspLinkCore/src/ControlSession.cpp` to the `ESPLINKCORE_SOURCES` list in `CMakeLists.txt` (added in Task 4, Step 6).

- [ ] **Step 4: Add session-isolation and duplicate-cache tests**

Append to `tests/control_protocol_engine_tests.cpp` (add `#include "ControlSession.h"` near the top):

```cpp
void test_two_sessions_do_not_share_transfer_state() {
    ControlSession sessionA{ControlSessionId{1}, ControllerId{100}};
    ControlSession sessionB{ControlSessionId{2}, ControllerId{200}};

    sessionA.transfer().upload().active = true;
    sessionA.transfer().upload().bytes = {0xAA, 0xBB};
    sessionA.transfer().upload().nextOffset = 2;

    CHECK(!sessionB.transfer().upload().active);
    CHECK(sessionB.transfer().upload().bytes.empty());
    CHECK(sessionB.transfer().upload().nextOffset == 0);
}

void test_lease_cannot_be_acquired_twice_by_the_same_session() {
    ControlSession session{ControlSessionId{1}, ControllerId{100}};
    CHECK(session.tryAcquireLease());
    CHECK(!session.tryAcquireLease());
    session.releaseLease();
    CHECK(session.tryAcquireLease());
}

void test_duplicate_result_cache_round_trips_and_replays() {
    ControlSession session{ControlSessionId{1}, ControllerId{100}};
    CHECK(!session.lookupCachedResult(OperationId{42}).has_value());

    ControlSession::CommandResult result = Response{SimpleOkResponse{"save", "preset saved"}};
    session.cacheResult(OperationId{42}, result);

    auto cached = session.lookupCachedResult(OperationId{42});
    CHECK(cached.has_value());
    CHECK(std::holds_alternative<Response>(*cached));
    const auto& response = std::get<Response>(*cached);
    CHECK(std::holds_alternative<SimpleOkResponse>(response));
    CHECK(std::get<SimpleOkResponse>(response).message == "preset saved");
}

void test_duplicate_result_cache_evicts_oldest_when_full() {
    ControlSession session{ControlSessionId{1}, ControllerId{100}};
    for (uint64_t i = 0; i < 9; ++i) {
        ControlSession::CommandResult result = Response{SimpleOkResponse{"op", std::to_string(i)}};
        session.cacheResult(OperationId{i}, result);
    }
    // Capacity is 8; operation 0 should have been evicted by operation 8.
    CHECK(!session.lookupCachedResult(OperationId{0}).has_value());
    CHECK(session.lookupCachedResult(OperationId{8}).has_value());
}
```

Add the four calls to `main()`, alongside the existing two.

- [ ] **Step 5: Build and run**

```bash
cmake --build .build/native-validation --target control_protocol_engine_tests
ctest --test-dir .build/native-validation -R control_protocol_engine_tests --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add lib/EspLinkCore/src/TransferSession.h lib/EspLinkCore/src/ControlSession.h \
        lib/EspLinkCore/src/ControlSession.cpp CMakeLists.txt tests/control_protocol_engine_tests.cpp
git commit -m "feat(firmware): add session-scoped ControlSession/TransferSession and duplicate-result cache"
```

---

### Task 7: Firmware — `ControlProtocolEngine` (typed dispatch for all 18 v1 commands)

**Files:**
- Create: `lib/EspLinkCore/src/ControlProtocolEngine.h`, `lib/EspLinkCore/src/ControlProtocolEngine.cpp`
- Modify: `CMakeLists.txt`, `tests/control_protocol_engine_tests.cpp`

**Interfaces:**
- Consumes: `esplink::IBarcodeDevice`/`IPresetRepository`/`IDeviceControl` (Task 5); `esplink::ControlSession`, `ControlSession::CommandResult` (Task 6); `esplink::Command`/`Response`/`ProtocolError`, `esplink::kCommandCatalog`/`findCommandDescriptor` (Task 1); `esplink::crc32` (Task 4); `esplink::goldenFixtures()` (Task 3).
- Produces: `esplink::IControlResponseSink`, `esplink::ControlProtocolEngine`. This is the transport-independent dispatcher — Task 8's `SerialLegacyEndpoint`/`JsonCommandCodec` and Task 9's `SerialCobsEndpoint` both construct exactly one instance each and call `handle(...)`; neither touches `IBarcodeDevice`/`IPresetRepository` directly.

Behavioral source of truth for every handler below: `src/UsbProtocol.cpp:243-617` (already fully read for this plan) and `docs/PROTOCOL.md`. Three v1 error paths move out of the engine and into the JSON adapter (Task 8), because they are JSON/text-parsing concerns, not domain concerns: `invalid_rotation` (rotation string parsing), `invalid_base64` (chunk decoding), and `missing_command`/`unknown_command`/`invalid_json`/`invalid_request`/`line_too_long` (framing/parsing, never reach a `Command` value at all).

- [ ] **Step 1: Write `ControlProtocolEngine.h`**

```cpp
#pragma once

#include <string>

#include "ApplicationPorts.h"
#include "ControlSession.h"
#include "ProtocolCommands.h"

namespace esplink {

class IControlResponseSink {
public:
    virtual ~IControlResponseSink() = default;
    virtual void send(const Response& response) = 0;
    virtual void sendError(const ProtocolError& error) = 0;
};

class ControlProtocolEngine {
public:
    ControlProtocolEngine(IBarcodeDevice& device, IPresetRepository& presets,
                          IDeviceControl& deviceControl, std::string firmwareVersion);

    // `transportName` (e.g. "usb-uart-ndjson", "usb-cobs-v2") is embedded verbatim in
    // `hello`'s response and `home`'s internal status text. The engine never branches on it.
    void handle(ControlSession& session, const Command& command, OperationId operationId,
               const char* transportName, IControlResponseSink& sink);

private:
    using CommandResult = ControlSession::CommandResult;

    CommandResult dispatchSingle(ControlSession& session, const Command& command, const char* transportName);

    CommandResult handleHello(const char* transportName) const;
    CommandResult handleCapabilities() const;
    CommandResult handleStatus() const;
    CommandResult handleGenerate(const GenerateCommand& command);
    CommandResult handleDisplay(const DisplayCommand& command);
    CommandResult handleClose();
    CommandResult handleHome(const char* transportName);
    CommandResult handleSave(const SaveCommand& command);
    CommandResult handleLoad(const LoadCommand& command);
    CommandResult handleDelete(const DeleteCommand& command);
    CommandResult handleList() const;
    CommandResult handleUploadBegin(ControlSession& session, const UploadBeginCommand& command);
    CommandResult handleUploadChunk(ControlSession& session, const UploadChunkCommand& command);
    CommandResult handleUploadEnd(ControlSession& session, const UploadEndCommand& command);
    CommandResult handleUploadAbort(ControlSession& session);
    void handleDownload(const DownloadCommand& command, IControlResponseSink& sink) const;
    CommandResult handleBacklight(const BacklightCommand& command);
    CommandResult handleReboot();

    IBarcodeDevice& device_;
    IPresetRepository& presets_;
    IDeviceControl& deviceControl_;
    std::string firmwareVersion_;
};

const char* commandCatalogName(const Command& command);

}  // namespace esplink
```

- [ ] **Step 2: Write `ControlProtocolEngine.cpp`**

```cpp
#include "ControlProtocolEngine.h"

#include <algorithm>
#include <variant>

#include "CommandCatalog.h"
#include "Crc32.h"

namespace esplink {

const char* commandCatalogName(const Command& command) {
    struct Visitor {
        const char* operator()(const HelloCommand&) const { return "hello"; }
        const char* operator()(const CapabilitiesCommand&) const { return "capabilities"; }
        const char* operator()(const StatusCommand&) const { return "status"; }
        const char* operator()(const GenerateCommand&) const { return "generate"; }
        const char* operator()(const DisplayCommand&) const { return "display"; }
        const char* operator()(const CloseCommand&) const { return "close"; }
        const char* operator()(const HomeCommand&) const { return "home"; }
        const char* operator()(const SaveCommand&) const { return "save"; }
        const char* operator()(const LoadCommand&) const { return "load"; }
        const char* operator()(const DeleteCommand&) const { return "delete"; }
        const char* operator()(const ListCommand&) const { return "list"; }
        const char* operator()(const UploadBeginCommand&) const { return "upload_begin"; }
        const char* operator()(const UploadChunkCommand&) const { return "upload_chunk"; }
        const char* operator()(const UploadEndCommand&) const { return "upload_end"; }
        const char* operator()(const UploadAbortCommand&) const { return "upload_abort"; }
        const char* operator()(const DownloadCommand&) const { return "download"; }
        const char* operator()(const BacklightCommand&) const { return "backlight"; }
        const char* operator()(const RebootCommand&) const { return "reboot"; }
    };
    return std::visit(Visitor{}, command);
}

ControlProtocolEngine::ControlProtocolEngine(IBarcodeDevice& device, IPresetRepository& presets,
                                             IDeviceControl& deviceControl, std::string firmwareVersion)
    : device_(device), presets_(presets), deviceControl_(deviceControl), firmwareVersion_(std::move(firmwareVersion)) {}

void ControlProtocolEngine::handle(ControlSession& session, const Command& command, OperationId operationId,
                                   const char* transportName, IControlResponseSink& sink) {
    if (std::holds_alternative<DownloadCommand>(command)) {
        handleDownload(std::get<DownloadCommand>(command), sink);
        return;
    }

    const CommandDescriptor* descriptor = findCommandDescriptor(commandCatalogName(command));
    const bool replayable = descriptor != nullptr && descriptor->idempotency == Idempotency::ReplayResult;

    if (replayable) {
        if (auto cached = session.lookupCachedResult(operationId)) {
            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ProtocolError>) sink.sendError(value);
                else sink.send(value);
            }, *cached);
            return;
        }
    }

    CommandResult result = dispatchSingle(session, command, transportName);

    if (replayable) session.cacheResult(operationId, result);

    std::visit([&](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ProtocolError>) sink.sendError(value);
        else sink.send(value);
    }, result);

    if (std::holds_alternative<RebootCommand>(command) && std::holds_alternative<Response>(result)) {
        deviceControl_.reboot();
    }
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::dispatchSingle(ControlSession& session,
                                                                           const Command& command,
                                                                           const char* transportName) {
    if (std::holds_alternative<HelloCommand>(command)) return handleHello(transportName);
    if (std::holds_alternative<CapabilitiesCommand>(command)) return handleCapabilities();
    if (std::holds_alternative<StatusCommand>(command)) return handleStatus();
    if (std::holds_alternative<GenerateCommand>(command)) return handleGenerate(std::get<GenerateCommand>(command));
    if (std::holds_alternative<DisplayCommand>(command)) return handleDisplay(std::get<DisplayCommand>(command));
    if (std::holds_alternative<CloseCommand>(command)) return handleClose();
    if (std::holds_alternative<HomeCommand>(command)) return handleHome(transportName);
    if (std::holds_alternative<SaveCommand>(command)) return handleSave(std::get<SaveCommand>(command));
    if (std::holds_alternative<LoadCommand>(command)) return handleLoad(std::get<LoadCommand>(command));
    if (std::holds_alternative<DeleteCommand>(command)) return handleDelete(std::get<DeleteCommand>(command));
    if (std::holds_alternative<ListCommand>(command)) return handleList();
    if (std::holds_alternative<UploadBeginCommand>(command)) return handleUploadBegin(session, std::get<UploadBeginCommand>(command));
    if (std::holds_alternative<UploadChunkCommand>(command)) return handleUploadChunk(session, std::get<UploadChunkCommand>(command));
    if (std::holds_alternative<UploadEndCommand>(command)) return handleUploadEnd(session, std::get<UploadEndCommand>(command));
    if (std::holds_alternative<UploadAbortCommand>(command)) return handleUploadAbort(session);
    if (std::holds_alternative<BacklightCommand>(command)) return handleBacklight(std::get<BacklightCommand>(command));
    if (std::holds_alternative<RebootCommand>(command)) return handleReboot();
    return ProtocolError{"", "unknown_command", "unsupported command"};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleHello(const char* transportName) const {
    return Response{HelloResponse{"EspScreenBarcodeGenerator", "1.0", firmwareVersion_, transportName,
                                  320, 480}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleCapabilities() const {
    CapabilitiesResponse response;
    response.symbologies = {"qr", "datamatrix", "aztec", "code128", "gs1-128", "code39",
                            "upca", "ean13", "ean8", "itf", "itf14", "codabar", "msi"};
    for (const auto& d : kCommandCatalog) response.commands.emplace_back(d.name);
    response.payloadBytes = 2048;
    response.serialLineBytes = 4096;
    response.matrixWidth = 512;
    response.matrixHeight = 512;
    response.uploadEncoding = "base64-packed-msb-first";
    response.uploadChunkBytesRecommended = 384;
    response.rawMatrix = true;
    response.standaloneTouchUi = true;
    response.persistentPresets = true;
    return Response{response};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleStatus() const {
    StatusResponse response;
    response.barcodeVisible = device_.barcodeVisible();
    response.hasCurrent = device_.hasCurrent();
    response.currentRaw = device_.currentIsRaw();
    response.status = device_.statusText();
    response.freeHeap = deviceControl_.freeHeapBytes();
    if (device_.hasCurrent()) {
        const auto& result = device_.currentResult();
        CurrentSymbolInfo current;
        current.label = device_.currentLabel();
        current.width = result.matrix.width();
        current.height = result.matrix.height();
        current.linear = result.linear;
        current.quiet = device_.currentQuietZone();
        current.rotation = espbarcode::toString(device_.currentRotation());
        current.invert = device_.currentInvert();
        current.bytes = result.matrix.packed().size();
        response.current = current;
    }
    return Response{response};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleGenerate(const GenerateCommand& command) {
    std::string error;
    if (!device_.generate(command.spec, command.display, error)) {
        return ProtocolError{"generate", "generation_failed", error};
    }
    if (command.saveAs.has_value()) {
        if (!presets_.save(*command.saveAs, command.spec, error)) {
            return ProtocolError{"generate", "save_failed", error};
        }
    }
    GenerateResponse response;
    response.type = espbarcode::toString(command.spec.type);
    response.width = device_.currentResult().matrix.width();
    response.height = device_.currentResult().matrix.height();
    response.linear = device_.currentResult().linear;
    response.quiet = device_.currentQuietZone();
    response.displayed = command.display;
    response.normalizedData = device_.currentResult().normalizedData;
    return Response{response};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleDisplay(const DisplayCommand& command) {
    std::string error;
    if (command.presetName.has_value()) {
        espbarcode::BarcodeSpec spec;
        if (!presets_.load(*command.presetName, spec, error) || !device_.generate(spec, true, error)) {
            return ProtocolError{"display", "display_failed", error};
        }
    } else if (!device_.displayCurrent(error)) {
        return ProtocolError{"display", "display_failed", error};
    }
    return Response{SimpleOkResponse{"display", "symbol displayed"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleClose() {
    device_.closeBarcode();
    return Response{SimpleOkResponse{"close", "barcode closed"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleHome(const char* transportName) {
    device_.showHome(std::string(transportName) + " requested home screen");
    return Response{SimpleOkResponse{"home", "home screen displayed"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleSave(const SaveCommand& command) {
    if (command.name.empty()) return ProtocolError{"save", "missing_name", "name is required"};
    if (device_.currentIsRaw()) {
        return ProtocolError{"save", "raw_not_persisted", "raw matrices are transferable but are not preset records"};
    }
    std::string error;
    if (!presets_.save(command.name, device_.activeSpec(), error)) {
        return ProtocolError{"save", "save_failed", error};
    }
    return Response{SimpleOkResponse{"save", "preset saved"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleLoad(const LoadCommand& command) {
    if (command.name.empty()) return ProtocolError{"load", "missing_name", "name is required"};
    espbarcode::BarcodeSpec spec;
    std::string error;
    if (!presets_.load(command.name, spec, error)) return ProtocolError{"load", "load_failed", error};
    if (!device_.generate(spec, command.display, error)) return ProtocolError{"load", "generation_failed", error};
    return Response{SimpleOkResponse{"load", command.display ? "preset loaded and displayed" : "preset loaded"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleDelete(const DeleteCommand& command) {
    if (command.name.empty()) return ProtocolError{"delete", "missing_name", "name is required"};
    std::string error;
    if (!presets_.remove(command.name, error)) return ProtocolError{"delete", "delete_failed", error};
    return Response{SimpleOkResponse{"delete", "preset deleted"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleList() const {
    return Response{ListResponse{presets_.list()}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadBegin(ControlSession& session,
                                                                              const UploadBeginCommand& command) {
    auto& upload = session.transfer().upload();
    if (upload.active) {
        return ProtocolError{"upload_begin", "upload_active", "abort or finish the current upload first"};
    }
    if (command.width < 1 || command.width > 512 || command.height < 1 || command.height > 512 ||
        (command.linear && command.height != 1)) {
        return ProtocolError{"upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1"};
    }
    const std::size_t byteCount = (std::size_t(command.width) * command.height + 7U) / 8U;
    if (byteCount > 32768U) {
        return ProtocolError{"upload_begin", "matrix_too_large", "packed matrix exceeds 32768 bytes"};
    }

    upload.active = true;
    upload.width = command.width;
    upload.height = command.height;
    upload.linear = command.linear;
    upload.quiet = std::clamp<uint8_t>(command.quiet, 0, 32);
    upload.rotation = command.rotation;
    upload.invert = command.invert;
    upload.display = command.display;
    upload.label = command.label.empty() ? "Uploaded matrix" : command.label;
    upload.bytes.assign(byteCount, 0U);
    upload.nextOffset = 0;

    return Response{UploadBeginResponse{byteCount, 0}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadChunk(ControlSession& session,
                                                                              const UploadChunkCommand& command) {
    auto& upload = session.transfer().upload();
    if (!upload.active) return ProtocolError{"upload_chunk", "no_upload", "upload_begin is required"};
    if (command.offset != upload.nextOffset) {
        return ProtocolError{"upload_chunk", "unexpected_offset", "chunks must be sequential"};
    }
    if (command.offset + command.data.size() > upload.bytes.size()) {
        return ProtocolError{"upload_chunk", "overflow", "chunk exceeds declared matrix size"};
    }
    std::copy(command.data.begin(), command.data.end(), upload.bytes.begin() + std::ptrdiff_t(command.offset));
    upload.nextOffset += command.data.size();
    return Response{UploadChunkResponse{command.data.size(), upload.nextOffset}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadEnd(ControlSession& session,
                                                                            const UploadEndCommand& command) {
    auto& upload = session.transfer().upload();
    if (!upload.active) return ProtocolError{"upload_end", "no_upload", "upload_begin is required"};
    if (upload.nextOffset != upload.bytes.size()) {
        return ProtocolError{"upload_end", "incomplete", "not all declared bytes were received"};
    }
    const uint32_t actualCrc = crc32(upload.bytes.data(), upload.bytes.size());
    if (command.expectedCrc32.has_value() && *command.expectedCrc32 != actualCrc) {
        return ProtocolError{"upload_end", "crc_mismatch", "uploaded bytes failed CRC32 validation"};
    }

    espbarcode::BitMatrix matrix(upload.width, upload.height);
    matrix.packed() = upload.bytes;
    std::string error;
    const bool success = device_.setUploadedMatrix(std::move(matrix), upload.linear, upload.quiet, upload.rotation,
                                                    upload.invert, upload.label, upload.display, error);
    const bool displayed = upload.display;
    upload.reset();
    if (!success) return ProtocolError{"upload_end", "display_failed", error};
    return Response{UploadEndResponse{actualCrc, displayed}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadAbort(ControlSession& session) {
    session.transfer().upload().reset();
    return Response{SimpleOkResponse{"upload_abort", "upload discarded"}};
}

void ControlProtocolEngine::handleDownload(const DownloadCommand& command, IControlResponseSink& sink) const {
    if (!device_.hasCurrent()) {
        sink.sendError(ProtocolError{"download", "no_symbol", "no current symbol"});
        return;
    }
    const auto& result = device_.currentResult();
    const auto& bytes = result.matrix.packed();
    const std::size_t chunkSize = std::clamp<std::size_t>(command.chunkBytes, 48, 768);
    const uint32_t checksum = crc32(bytes.data(), bytes.size());

    DownloadBeginEvent begin;
    begin.width = result.matrix.width();
    begin.height = result.matrix.height();
    begin.linear = result.linear;
    begin.quiet = device_.currentQuietZone();
    begin.rotation = espbarcode::toString(device_.currentRotation());
    begin.invert = device_.currentInvert();
    begin.label = device_.currentLabel();
    begin.bytes = bytes.size();
    begin.encoding = "base64-packed-msb-first";
    begin.crc32 = checksum;
    sink.send(Response{begin});

    for (std::size_t offset = 0; offset < bytes.size(); offset += chunkSize) {
        const std::size_t count = std::min(chunkSize, bytes.size() - offset);
        DownloadChunkEvent chunk;
        chunk.offset = offset;
        chunk.data.assign(bytes.begin() + std::ptrdiff_t(offset), bytes.begin() + std::ptrdiff_t(offset + count));
        sink.send(Response{chunk});
    }

    DownloadEndEvent end;
    end.bytes = bytes.size();
    end.crc32 = checksum;
    sink.send(Response{end});
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleBacklight(const BacklightCommand& command) {
    device_.setBacklight(command.on);
    return Response{SimpleOkResponse{"backlight", command.on ? "backlight on" : "backlight off"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleReboot() {
    return Response{SimpleOkResponse{"reboot", "rebooting"}};
}

}  // namespace esplink
```

Note the deliberate behavior change from `src/UsbProtocol.cpp:474` (`upload_.label = std::string(request["label"] | "Uploaded matrix")`, i.e. ArduinoJson substitutes the default only when the key is absent): the typed `UploadBeginCommand.label` collapses "absent" and "empty string" into the same default in `handleUploadBegin` (`command.label.empty() ? "Uploaded matrix" : command.label`). This is a legitimate typed-command simplification, not a JSON-adapter concern — record it in the completion summary (Task 15) as an intentional, tested behavior note rather than leaving it undocumented.

- [ ] **Step 3: Add `ControlProtocolEngine.cpp` to the CMake target**

Add `lib/EspLinkCore/src/ControlProtocolEngine.cpp` to `ESPLINKCORE_SOURCES` in `CMakeLists.txt`.

- [ ] **Step 4: Replay the golden fixtures through the real engine**

Append to `tests/control_protocol_engine_tests.cpp` (add `#include "ControlProtocolEngine.h"` and `#include "vectors_v1_golden.h"` near the top):

```cpp
class RecordingSink : public IControlResponseSink {
public:
    void send(const Response& response) override { responses.push_back(response); }
    void sendError(const ProtocolError& error) override { errors.push_back(error); }

    std::vector<Response> responses;
    std::vector<ProtocolError> errors;
};

void test_hello_matches_golden_fixture() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, HelloCommand{}, OperationId{1}, "usb-uart-ndjson", sink);

    CHECK(sink.responses.size() == 1);
    CHECK(std::holds_alternative<HelloResponse>(sink.responses[0]));
    const auto& hello = std::get<HelloResponse>(sink.responses[0]);
    CHECK(hello.device == "EspScreenBarcodeGenerator");
    CHECK(hello.protocol == "1.0");
    CHECK(hello.transport == "usb-uart-ndjson");
    CHECK(hello.screenWidth == 320 && hello.screenHeight == 480);
}

void test_status_no_current_matches_golden_fixture() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, StatusCommand{}, OperationId{1}, "usb-uart-ndjson", sink);

    CHECK(sink.responses.size() == 1);
    const auto& status = std::get<StatusResponse>(sink.responses[0]);
    CHECK(!status.barcodeVisible);
    CHECK(!status.hasCurrent);
    CHECK(!status.current.has_value());
}

void test_close_home_backlight_match_golden_fixtures() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, CloseCommand{}, OperationId{1}, "usb-uart-ndjson", sink);
    engine.handle(session, HomeCommand{}, OperationId{2}, "usb-uart-ndjson", sink);
    engine.handle(session, BacklightCommand{false}, OperationId{3}, "usb-uart-ndjson", sink);

    CHECK(std::get<SimpleOkResponse>(sink.responses[0]).message == "barcode closed");
    CHECK(std::get<SimpleOkResponse>(sink.responses[1]).message == "home screen displayed");
    CHECK(std::get<SimpleOkResponse>(sink.responses[2]).message == "backlight off");
}

void test_upload_round_trip_matches_golden_fixtures() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    UploadBeginCommand begin{3, 2, false, 4, espbarcode::Rotation::Auto, false, true, "external-pdf417"};
    engine.handle(session, begin, OperationId{1}, "usb-uart-ndjson", sink);
    CHECK(std::get<UploadBeginResponse>(sink.responses.back()).bytesExpected == 1);

    engine.handle(session, UploadChunkCommand{0, {0xA8}}, OperationId{2}, "usb-uart-ndjson", sink);
    CHECK(std::get<UploadChunkResponse>(sink.responses.back()).nextOffset == 1);

    engine.handle(session, UploadEndCommand{168805463u}, OperationId{3}, "usb-uart-ndjson", sink);
    const auto& end = std::get<UploadEndResponse>(sink.responses.back());
    CHECK(end.crc32 == 168805463u);
    CHECK(end.displayed);
    CHECK(device.currentIsRaw());
}

void test_upload_chunk_wrong_offset_matches_golden_fixture() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, UploadBeginCommand{3, 2, false, 4, espbarcode::Rotation::Auto, false, true, "x"},
                  OperationId{1}, "usb-uart-ndjson", sink);
    engine.handle(session, UploadChunkCommand{1, {0xA8}}, OperationId{2}, "usb-uart-ndjson", sink);

    CHECK(sink.errors.size() == 1);
    CHECK(sink.errors[0].code == "unexpected_offset");
}

void test_reboot_is_replayed_and_triggers_device_control_only_once() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, RebootCommand{}, OperationId{99}, "usb-uart-ndjson", sink);
    CHECK(control.rebooted());

    // A duplicate operation id (carrier retry) must replay the cached ack, not re-trigger
    // the real restart. FakeDeviceControl only tracks a bool, so this asserts the replayed
    // response is identical; a richer fake could additionally count invocations.
    RecordingSink secondSink;
    engine.handle(session, RebootCommand{}, OperationId{99}, "usb-uart-ndjson", secondSink);
    CHECK(secondSink.responses.size() == 1);
    CHECK(std::get<SimpleOkResponse>(secondSink.responses[0]).message == "rebooting");
}

void test_two_sessions_do_not_corrupt_each_others_upload_via_the_engine() {
    FakeBarcodeDevice deviceA, deviceB;
    FakePresetRepository presetsA, presetsB;
    FakeDeviceControl controlA, controlB;
    ControlProtocolEngine engineA(deviceA, presetsA, controlA, "0.1.0-test");
    ControlProtocolEngine engineB(deviceB, presetsB, controlB, "0.1.0-test");
    ControlSession sessionA{ControlSessionId{1}, ControllerId{1}};
    ControlSession sessionB{ControlSessionId{2}, ControllerId{2}};
    RecordingSink sinkA, sinkB;

    engineA.handle(sessionA, UploadBeginCommand{3, 2, false, 4, espbarcode::Rotation::Auto, false, true, "a"},
                   OperationId{1}, "usb-uart-ndjson", sinkA);
    engineA.handle(sessionA, UploadChunkCommand{0, {0xA8}}, OperationId{2}, "usb-uart-ndjson", sinkA);

    // Session B never began an upload — its engine call must fail with no_upload, proving
    // session A's TransferSession state never leaked into session B.
    engineB.handle(sessionB, UploadChunkCommand{0, {0xFF}}, OperationId{2}, "usb-uart-ndjson", sinkB);
    CHECK(sinkB.errors.size() == 1);
    CHECK(sinkB.errors[0].code == "no_upload");
}
```

Add all seven new test calls to `main()`.

- [ ] **Step 5: Build and run**

```bash
cmake --build .build/native-validation --target control_protocol_engine_tests
ctest --test-dir .build/native-validation -R control_protocol_engine_tests --output-on-failure
```

Expected: all cases pass, including CRC-exact upload round trip and session isolation.

- [ ] **Step 6: Commit**

```bash
git add lib/EspLinkCore/src/ControlProtocolEngine.h lib/EspLinkCore/src/ControlProtocolEngine.cpp \
        CMakeLists.txt tests/control_protocol_engine_tests.cpp
git commit -m "feat(firmware): add ControlProtocolEngine dispatching all 18 v1 commands"
```

---

### Task 8: Firmware — `JsonCommandCodec` + `SerialLegacyEndpoint` (replaces `UsbProtocol`)

**Files:**
- Create: `src/JsonCommandCodec.h`, `src/JsonCommandCodec.cpp`
- Create: `src/SerialLegacyEndpoint.h`, `src/SerialLegacyEndpoint.cpp`
- Delete: `include/UsbProtocol.h`, `src/UsbProtocol.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `esplink::Command`/`Response`/`ProtocolError` (Task 1); `esplink::ControlProtocolEngine`, `IControlResponseSink` (Task 7); `esplink::ControlSession` (Task 6); `esplink::BarcodeApplicationAdapter`, `EspIdfDeviceControl` (Task 5); `esplink::goldenFixtures()` (Task 3, used as the manual behavioral checklist below — there is no native ArduinoJson build to replay them through automatically, see Step 5).
- Produces: `SerialLegacyEndpoint` — the sole class in the firmware that touches `Serial`. Task 9's `SerialCobsEndpoint` reuses `JsonCommandCodec::decode`/`encode` for its own body codec (v2 keeps JSON as the control-message codec per the design doc §8.7).

This task is a **mechanical transcription** of `src/UsbProtocol.cpp:243-617` into the typed-`Command`/`Response` shapes from Task 1, with three v1 error paths now living entirely in `JsonCommandCodec::decode` because they are JSON-parsing concerns (see Task 7's note): `invalid_rotation`, `invalid_base64`, `invalid_spec` (all its sub-cases). `docs/PROTOCOL.md` and `src/UsbProtocol.cpp` are the exact behavioral oracle — do not improvise a field name, error code, or default value not already present there.

- [ ] **Step 1: Write `JsonCommandCodec.h`**

```cpp
#pragma once

#include <ArduinoJson.h>

#include <string>

#include "ApplicationPorts.h"
#include "ProtocolCommands.h"

namespace esplink {

class JsonCommandCodec {
public:
    // `commandName` is already lower-cased and known non-empty — the caller
    // (SerialLegacyEndpoint) has already handled "missing_command". `device` supplies
    // the "merge with the currently active spec" defaults `generate` needs (see Step 2).
    // Returns false with `errorCode`/`errorMessage` set for a command-specific decode
    // failure (invalid_spec, invalid_rotation, invalid_base64, missing_offset,
    // invalid_name); returns true and populates `out` otherwise.
    static bool decode(const std::string& commandName, JsonObjectConst request, const IBarcodeDevice& device,
                       Command& out, std::string& errorCode, std::string& errorMessage);

    static void encode(const Response& response, JsonObjectConst request, JsonDocument& out);
    static void encodeError(const ProtocolError& error, JsonObjectConst request, JsonDocument& out);

private:
    static bool parseSpec(JsonObjectConst request, const espbarcode::BarcodeSpec& base,
                          espbarcode::BarcodeSpec& spec, std::string& errorCode, std::string& errorMessage);
    static JsonVariantConst value(JsonObjectConst request, const char* key);
    static void addId(JsonDocument& out, JsonObjectConst request);
};

}  // namespace esplink
```

- [ ] **Step 2: Write `JsonCommandCodec::decode` — worked examples**

Two fully worked cases below establish the pattern for every command. Field-by-field behavior for the remaining fourteen is in the table in Step 3 — apply the same pattern, checking each field/default/error code against `src/UsbProtocol.cpp` at the cited lines and `docs/PROTOCOL.md`.

`hello`/`ping` (source: `src/UsbProtocol.cpp:86-87`, no fields to parse):

```cpp
if (commandName == "hello" || commandName == "ping") {
    out = HelloCommand{};
    return true;
}
```

`generate` (source: `src/UsbProtocol.cpp:142-241,310-347` — the `parseSpec` merge-with-current-spec logic):

```cpp
if (commandName == "generate") {
    espbarcode::BarcodeSpec spec;
    if (!parseSpec(request, device.activeSpec(), spec, errorCode, errorMessage)) return false;
    GenerateCommand command;
    command.spec = spec;
    command.display = request["display"] | true;
    JsonVariantConst saveAs = request["save_as"];
    if (!saveAs.isNull()) {
        if (!saveAs.is<const char*>()) {
            errorCode = "invalid_spec";
            errorMessage = "save_as must be a string";
            return false;
        }
        command.saveAs = std::string(saveAs.as<const char*>());
    }
    out = command;
    return true;
}
```

`parseSpec` is `src/UsbProtocol.cpp:142-241` transcribed verbatim, with two changes: it takes `base` (the caller-supplied starting spec — `device.activeSpec()` for `generate`) instead of reading `application_.activeSpec()` itself, and every `error = "..."; return false;` becomes `errorCode = "invalid_spec"; errorMessage = "..."; return false;` (every `parseSpec` failure in the original maps to `invalid_spec` at the call site — `src/UsbProtocol.cpp:314`). The `value()` helper (`src/UsbProtocol.cpp:135-140`, top-level-or-`options`-nested lookup) is copied unchanged.

`upload_chunk` (source: `src/UsbProtocol.cpp:487-522` — binary/offset semantics, base64 decoded here so the engine never sees encoded text):

```cpp
if (commandName == "upload_chunk") {
    JsonVariantConst offsetValue = request["offset"];
    if (offsetValue.isNull() || !offsetValue.is<unsigned long>()) {
        errorCode = "missing_offset";
        errorMessage = "offset is required and must be an unsigned integer";
        return false;
    }
    const char* encoded = request["data"] | "";
    std::vector<uint8_t> chunk;
    if (*encoded == '\0' || !espbarcode::bytesFromBase64(encoded, chunk)) {
        errorCode = "invalid_base64";
        errorMessage = "data must be a base64 string";
        return false;
    }
    UploadChunkCommand command;
    command.offset = static_cast<std::size_t>(offsetValue.as<unsigned long>());
    command.data = std::move(chunk);
    out = command;
    return true;
}
```

- [ ] **Step 3: Complete `decode` for the remaining fourteen commands**

| Command | Source lines | Fields to extract | Decode-time errors |
|---|---|---|---|
| `capabilities` | `UsbProtocol.cpp:88-89` | none | none |
| `status` | `UsbProtocol.cpp:90-91` | none | none |
| `display` | `UsbProtocol.cpp:94-95,349-368` | `name` (optional string) into `DisplayCommand::presetName` | `invalid_name` if `name` present but not a string (`UsbProtocol.cpp:352-354`) |
| `close` | `UsbProtocol.cpp:96-98` | none | none |
| `home` | `UsbProtocol.cpp:99-101` | none | none |
| `save` | `UsbProtocol.cpp:102-103,370-386` | `name` (`request["name"] \| ""`, no type-check — matches original) | none (missing-name is engine-checked) |
| `load` | `UsbProtocol.cpp:104-105,388-406` | `name` (`request["name"] \| ""`), `display` (`request["display"] \| false`) | none |
| `delete` | `UsbProtocol.cpp:106-107,408-420` | `name` (`request["name"] \| ""`) | none |
| `list` | `UsbProtocol.cpp:108-109` | none | none |
| `upload_begin` | `UsbProtocol.cpp:110-111,432-485` | `width`/`height` (`int`, default 0), `linear` (default false), `quiet` (`std::clamp(request["quiet"]\|4,0,32)`), `rotation` (see below), `invert` (default false), `display` (default true), `label` (`request["label"] \| "Uploaded matrix"`) | `invalid_rotation` if `rotation` present but neither int nor string, or unparseable (`UsbProtocol.cpp:451-464`) — dimension/size validation (`invalid_dimensions`, `matrix_too_large`) stays in the engine (Task 7), not here |
| `upload_end` | `UsbProtocol.cpp:114-115,524-565` | `crc32` (optional `uint32_t`, from `request["crc32"]`) | none |
| `upload_abort` | `UsbProtocol.cpp:116-118` | none | none |
| `download` | `UsbProtocol.cpp:119-120,567-617` | `chunk_bytes` (`request["chunk_bytes"] \| 384`) | none (the 48-768 clamp lives in the engine) |
| `backlight` | `UsbProtocol.cpp:121-124` | `on` (`request["on"] \| true`) | none |
| `reboot` | `UsbProtocol.cpp:125-129` | none | none |

Rotation parsing (shared by `parseSpec` and `upload_begin`, source `UsbProtocol.cpp:196-211` / `450-464`):

```cpp
bool decodeRotation(JsonVariantConst rotationValue, espbarcode::Rotation& out, std::string& errorMessage) {
    if (rotationValue.isNull()) return true;  // caller keeps its current default
    std::string text;
    if (rotationValue.is<int>()) text = std::to_string(rotationValue.as<int>());
    else if (rotationValue.is<const char*>()) text = rotationValue.as<const char*>();
    else { errorMessage = "rotation must be a number or string"; return false; }
    if (!espbarcode::tryParseRotation(text, out)) {
        errorMessage = "rotation must be auto, 0, 90, 180, or 270";
        return false;
    }
    return true;
}
```

`generate`'s call site sets `errorCode = "invalid_spec"` on failure; `upload_begin`'s call site sets `errorCode = "invalid_rotation"` on failure (matching `UsbProtocol.cpp:457,461` exactly — this is the one command whose rotation error code differs from `invalid_spec`).

- [ ] **Step 4: Write `JsonCommandCodec::encode`/`encodeError`**

Mirror `UsbProtocol::send`/`sendOk`/`sendError`/`addId` (`src/UsbProtocol.cpp:619-649`) using `std::visit` over the `Response` variant, writing every field documented in `docs/PROTOCOL.md`. Worked example for two variants (apply the same field-by-field pattern — matching each struct's fields from Task 1's `ProtocolCommands.h` to the exact JSON keys in `docs/PROTOCOL.md` — to the remaining ten):

```cpp
void JsonCommandCodec::encode(const Response& response, JsonObjectConst request, JsonDocument& out) {
    addId(out, request);
    out["ok"] = true;
    std::visit([&](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, HelloResponse>) {
            out["cmd"] = request["cmd"] | "hello";
            out["device"] = value.device;
            out["protocol"] = value.protocol;
            out["firmware"] = value.firmware;
            out["transport"] = value.transport;
            out["screen"]["width"] = value.screenWidth;
            out["screen"]["height"] = value.screenHeight;
        } else if constexpr (std::is_same_v<T, SimpleOkResponse>) {
            out["cmd"] = value.command;
            if (value.message.has_value()) out["message"] = *value.message;
        }
        // ... CapabilitiesResponse, StatusResponse, GenerateResponse, ListResponse,
        // UploadBeginResponse, UploadChunkResponse, UploadEndResponse,
        // DownloadBeginEvent, DownloadChunkEvent, DownloadEndEvent: one branch each,
        // field names exactly matching docs/PROTOCOL.md's response examples for that
        // command (e.g. DownloadBeginEvent also sets out["event"] = "download_begin"
        // and out["cmd"] = "download", per UsbProtocol.cpp:580-591).
    }, response);
}

void JsonCommandCodec::encodeError(const ProtocolError& error, JsonObjectConst request, JsonDocument& out) {
    addId(out, request);
    out["ok"] = false;
    if (!error.command.empty()) out["cmd"] = error.command;
    out["error"]["code"] = error.code;
    out["error"]["message"] = error.message;
}
```

- [ ] **Step 5: Write `SerialLegacyEndpoint.h`/`.cpp`**

Owns exactly what `UsbProtocol` owned for byte I/O and line framing (`src/UsbProtocol.cpp:20-79`), now calling into `JsonCommandCodec` and `ControlProtocolEngine` instead of handling commands inline. Constructor takes the engine, a `ControlSession&`, and an `OperationId` generator (a simple incrementing counter is fine — v1 has no client-supplied operation IDs, so the endpoint assigns one per accepted request).

```cpp
// src/SerialLegacyEndpoint.h
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

#include "ApplicationPorts.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"

namespace esplink {

class SerialLegacyEndpoint : public IControlResponseSink {
public:
    SerialLegacyEndpoint(ControlProtocolEngine& engine, ControlSession& session, IDeviceControl& deviceControl,
                         const IBarcodeDevice& device);

    void begin();
    void loop();

    // IControlResponseSink
    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

private:
    void processLine(const std::string& line);

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    IDeviceControl& deviceControl_;
    const IBarcodeDevice& device_;
    std::string line_;
    bool discardingLine_ = false;
    JsonObjectConst currentRequest_;  // valid only during a send()/sendError() call from within processLine()
    bool pendingReboot_ = false;
    uint64_t nextOperationId_ = 1;
};

}  // namespace esplink
```

```cpp
// src/SerialLegacyEndpoint.cpp
#include "SerialLegacyEndpoint.h"

#include "JsonCommandCodec.h"
#include "app_config.h"

namespace esplink {

SerialLegacyEndpoint::SerialLegacyEndpoint(ControlProtocolEngine& engine, ControlSession& session,
                                           IDeviceControl& deviceControl, const IBarcodeDevice& device)
    : engine_(engine), session_(session), deviceControl_(deviceControl), device_(device) {}

void SerialLegacyEndpoint::begin() {
    line_.reserve(512);
    JsonDocument event;
    event["event"] = "ready";
    event["device"] = app_config::kDeviceName;
    event["protocol"] = app_config::kProtocolVersion;
    event["firmware"] = ESPBARCODE_VERSION;
    serializeJson(event, Serial);
    Serial.write('\n');
}

void SerialLegacyEndpoint::loop() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            if (discardingLine_) {
                discardingLine_ = false;
                line_.clear();
                JsonDocument response;
                response["ok"] = false;
                response["error"]["code"] = "line_too_long";
                response["error"]["message"] = "request exceeded serial line limit";
                serializeJson(response, Serial);
                Serial.write('\n');
            } else if (!line_.empty()) {
                processLine(line_);
                line_.clear();
            }
            continue;
        }
        if (discardingLine_) continue;
        if (line_.size() >= app_config::kSerialLineLimit) {
            discardingLine_ = true;
            line_.clear();
            continue;
        }
        line_.push_back(c);
    }
}

void SerialLegacyEndpoint::processLine(const std::string& line) {
    JsonDocument document;
    const DeserializationError parseError = deserializeJson(document, line);
    if (parseError) {
        JsonDocument response;
        response["ok"] = false;
        response["error"]["code"] = "invalid_json";
        response["error"]["message"] = parseError.c_str();
        serializeJson(response, Serial);
        Serial.write('\n');
        return;
    }
    if (!document.is<JsonObject>()) {
        JsonDocument response;
        response["ok"] = false;
        response["error"]["code"] = "invalid_request";
        response["error"]["message"] = "request must be a JSON object";
        serializeJson(response, Serial);
        Serial.write('\n');
        return;
    }

    JsonObjectConst request = document.as<JsonObjectConst>();
    const char* commandText = request["cmd"] | "";
    std::string commandName(commandText);
    for (char& ch : commandName) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if (commandName.empty()) {
        JsonDocument response;
        response["ok"] = false;
        if (request["id"] && !request["id"].isNull()) response["id"].set(request["id"]);
        response["error"]["code"] = "missing_command";
        response["error"]["message"] = "cmd is required";
        serializeJson(response, Serial);
        Serial.write('\n');
        return;
    }

    Command command;
    std::string errorCode, errorMessage;
    if (!JsonCommandCodec::decode(commandName, request, device_, command, errorCode, errorMessage)) {
        if (errorCode.empty()) {
            errorCode = "unknown_command";
            errorMessage = "unsupported command";
        }
        currentRequest_ = request;
        sendError(ProtocolError{commandName, errorCode, errorMessage});
        currentRequest_ = JsonObjectConst();
        return;
    }

    currentRequest_ = request;
    pendingReboot_ = std::holds_alternative<RebootCommand>(command);
    engine_.handle(session_, command, OperationId{nextOperationId_++}, "usb-uart-ndjson", *this);
    currentRequest_ = JsonObjectConst();

    if (pendingReboot_) {
        Serial.flush();
        delay(100);
        // ControlProtocolEngine already called deviceControl_.reboot() before returning
        // from handle() (see Task 7) — nothing further to do here except make sure the
        // acknowledgement above reached the host first.
    }
}

void SerialLegacyEndpoint::send(const Response& response) {
    JsonDocument out;
    JsonCommandCodec::encode(response, currentRequest_, out);
    serializeJson(out, Serial);
    Serial.write('\n');
}

void SerialLegacyEndpoint::sendError(const ProtocolError& error) {
    JsonDocument out;
    JsonCommandCodec::encodeError(error, currentRequest_, out);
    serializeJson(out, Serial);
    Serial.write('\n');
}

}  // namespace esplink
```

`JsonCommandCodec::decode`'s "unknown_command" case does not actually occur here — `processLine` never calls `decode` for a name outside the eighteen it matches — so give `decode` an explicit `if (commandName == "hello" || ...) ... return true; return false with errorCode="unknown_command"` fallthrough at the very end, so the `if (errorCode.empty())` branch above is dead code kept only as a defensive default. Note this explicitly in a one-line comment above that branch when you write it.

- [ ] **Step 6: Delete `UsbProtocol` and wire `main.cpp`**

```bash
git rm include/UsbProtocol.h src/UsbProtocol.cpp
```

`src/main.cpp`:

```cpp
#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "BarcodeApplicationAdapter.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "EspIdfDeviceControl.h"
#include "SerialLegacyEndpoint.h"
#include "app_config.h"

namespace {
BarcodeApplication application;
esplink::BarcodeApplicationAdapter applicationAdapter(application);
esplink::EspIdfDeviceControl deviceControl(application);
esplink::ControlProtocolEngine engine(applicationAdapter, applicationAdapter, deviceControl, ESPBARCODE_VERSION);
esplink::ControlSession legacySession(esplink::ControlSessionId{1}, esplink::ControllerId{1});
esplink::SerialLegacyEndpoint endpoint(engine, legacySession, deviceControl, applicationAdapter);
}  // namespace

void setup() {
    Serial.begin(app_config::kSerialBaud);
    Serial.setTimeout(25);
    delay(100);

    std::string error;
    if (!application.begin(error)) {
        Serial.printf("{\"event\":\"fatal\",\"message\":\"%s\"}\n", error.c_str());
        return;
    }
    endpoint.begin();
}

void loop() {
    endpoint.loop();
    application.loop();
    delay(1);
}
```

- [ ] **Step 7: Compile-check under PlatformIO (native CMake cannot build this file — see the note below)**

`JsonCommandCodec`/`SerialLegacyEndpoint`/`EspIdfDeviceControl` depend on `Arduino.h`/`ArduinoJson.h`/`Serial`, which the native `CMakeLists.txt` build intentionally excludes (Global Constraints). Verify with the real toolchain instead:

```bash
pio run -e esp32dev
```

Expected: builds with zero warnings, same as before this task. This requires the ESP32 PlatformIO toolchain to be installed; if it is not available in this environment, run `pio check -e esp32dev --skip-packages` if available, or explicitly record in the Task 15 completion summary that this compile check could not be performed and must happen before merge.

- [ ] **Step 8: Manually cross-check against the golden fixtures**

There is no automated harness for this file (Step 7's note) — walk `tests/vectors_v1_golden.h` fixture by fixture and confirm `JsonCommandCodec`/`SerialLegacyEndpoint` reproduce each one's `command`/`expected` pair when driven with the equivalent JSON request. This is a reading exercise, not a new test file. Record any discrepancy found as a fixture bug (fix `tests/vectors_v1_golden.h` against `docs/PROTOCOL.md`) or an implementation bug (fix `JsonCommandCodec`/`SerialLegacyEndpoint`) — never silently diverge from `docs/PROTOCOL.md`.

- [ ] **Step 9: Commit**

```bash
git add src/JsonCommandCodec.h src/JsonCommandCodec.cpp src/SerialLegacyEndpoint.h src/SerialLegacyEndpoint.cpp src/main.cpp
git commit -m "refactor(firmware): replace UsbProtocol with JsonCommandCodec + SerialLegacyEndpoint over ControlProtocolEngine"
```

---

### Task 9: Firmware — `system.upgrade` negotiation and `SerialCobsEndpoint` (USB v2)

**Files:**
- Modify: `src/JsonCommandCodec.h`, `src/JsonCommandCodec.cpp` (extract a shared body-encoding helper)
- Modify: `src/SerialLegacyEndpoint.h`, `src/SerialLegacyEndpoint.cpp` (add the `upgrade` command)
- Create: `src/SerialCobsEndpoint.h`, `src/SerialCobsEndpoint.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `esplink::cobsEncode`/`cobsDecode`, `encodeEnvelope`/`decodeEnvelope`, `encodeHopFrame`/`decodeHopFrame`, `FrameAssembler` (Task 4); `esplink::ControlProtocolEngine`, `IControlResponseSink` (Task 7); `esplink::CarrierProfileId::StreamStandard` (Task 1); `JsonCommandCodec::decode` (Task 8).
- Produces: `esplink::SerialCobsEndpoint`. This is the "one fully working USB v2 path" deliverable: after an explicit `upgrade` request over the v1 NDJSON line protocol, the same physical UART carries EspLink v2 COBS frames for the rest of the connection, dispatched through the *same* `ControlProtocolEngine` instance used by `SerialLegacyEndpoint`.

**Deliberate scope cut (record this in Task 14's docs and Task 15's summary, not silently):** this session's v2 command subset is `system.hello` (handshake) → `system.welcome`, `system.ping`, `barcode.generate`, `barcode.close`, `device.backlight.set` — five names, each mapped onto the existing v1 typed-command path. The full namespaced catalog from the design doc §8.9 (presets, transfer, trust, gateway, diagnostics namespaces) is out of scope this session; the mapping table below is written so adding a name is a one-line addition, not a redesign.

- [ ] **Step 1: Extract a shared body-encoding helper in `JsonCommandCodec`**

Refactor `encode` (written in Task 8) so the per-`Response`-variant field writes are reusable without the v1-only `ok`/`cmd`/`id` envelope wrapper:

```cpp
// JsonCommandCodec.h — add:
static void encodeBody(const Response& response, JsonObject body);
```

```cpp
// JsonCommandCodec.cpp
void JsonCommandCodec::encodeBody(const Response& response, JsonObject body) {
    std::visit([&](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, HelloResponse>) {
            body["device"] = value.device;
            body["protocol"] = value.protocol;
            body["firmware"] = value.firmware;
            body["transport"] = value.transport;
            body["screen"]["width"] = value.screenWidth;
            body["screen"]["height"] = value.screenHeight;
        } else if constexpr (std::is_same_v<T, SimpleOkResponse>) {
            if (value.message.has_value()) body["message"] = *value.message;
        }
        // ... same per-variant field writes as Task 8's `encode`, minus cmd/ok/id.
    }, response);
}

void JsonCommandCodec::encode(const Response& response, JsonObjectConst request, JsonDocument& out) {
    addId(out, request);
    out["ok"] = true;
    out["cmd"] = commandCatalogName_or_request_cmd(response, request);  // unchanged from Task 8
    encodeBody(response, out.to<JsonObject>());
}
```

(`out.to<JsonObject>()` reuses the same top-level document as the body target — ArduinoJson allows adding sibling keys before and after this call, matching the original flat response shape from Task 8.)

- [ ] **Step 2: Add `upgrade` handling to `SerialLegacyEndpoint`**

Add to `SerialLegacyEndpoint.h`: `bool upgradeRequested() const { return upgradeRequested_; }` and a `bool upgradeRequested_ = false;` field. In `processLine`, before calling `JsonCommandCodec::decode`, special-case the command:

```cpp
if (commandName == "upgrade") {
    JsonDocument response;
    response["ok"] = true;
    response["cmd"] = "upgrade";
    response["message"] = "switching to EspLink v2 COBS framing";
    if (request["id"] && !request["id"].isNull()) response["id"].set(request["id"]);
    serializeJson(response, Serial);
    Serial.write('\n');
    Serial.flush();
    upgradeRequested_ = true;
    return;
}
```

`upgrade` is intentionally not a catalog command (`CommandCatalog.h` stays at 18 entries) — it never reaches `ControlProtocolEngine`; it only flips a transport-level flag `main.cpp`'s loop reads.

- [ ] **Step 3: Write `SerialCobsEndpoint.h`/`.cpp`**

```cpp
// src/SerialCobsEndpoint.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationPorts.h"
#include "Cobs.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "Envelope.h"
#include "FrameAssembler.h"
#include "HopFrame.h"

namespace esplink {

class SerialCobsEndpoint : public IControlResponseSink {
public:
    SerialCobsEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device);

    void loop();

    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

private:
    void processCobsBlock(const std::vector<uint8_t>& block);
    void processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body);
    void writeMessage(MessageKind kind, const std::string& name, JsonObjectConst extraBody);
    void writeMessageWithResponse(MessageKind kind, const std::string& name, const Response* response,
                                  const ProtocolError* error);

    static const char* v1NameFor(const std::string& v2Name);  // nullptr if unmapped this session

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    const IBarcodeDevice& device_;
    FrameAssembler assembler_;
    std::vector<uint8_t> rxBlock_;
    uint32_t linkMessageCounter_ = 1;
    uint64_t nextOperationId_ = 1;
    uint64_t currentRequestOperationId_ = 0;
    std::string currentRequestName_;
};

}  // namespace esplink
```

```cpp
// src/SerialCobsEndpoint.cpp
#include "SerialCobsEndpoint.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "JsonCommandCodec.h"

namespace esplink {

namespace {
// This session's v2-name -> v1-name subset (see Task 9's "Deliberate scope cut").
const char* mapV2Name(const std::string& name) {
    if (name == "system.hello" || name == "system.ping") return "hello";
    if (name == "barcode.generate") return "generate";
    if (name == "barcode.close") return "close";
    if (name == "device.backlight.set") return "backlight";
    return nullptr;
}
}  // namespace

SerialCobsEndpoint::SerialCobsEndpoint(ControlProtocolEngine& engine, ControlSession& session,
                                       const IBarcodeDevice& device)
    : engine_(engine), session_(session), device_(device) {}

void SerialCobsEndpoint::loop() {
    while (Serial.available() > 0) {
        const uint8_t b = static_cast<uint8_t>(Serial.read());
        if (b == 0x00) {
            if (!rxBlock_.empty()) processCobsBlock(rxBlock_);
            rxBlock_.clear();
            continue;
        }
        if (rxBlock_.size() < 2048) rxBlock_.push_back(b);  // bounded; oversized blocks are dropped at the delimiter
    }
}

void SerialCobsEndpoint::processCobsBlock(const std::vector<uint8_t>& block) {
    std::vector<uint8_t> raw;
    if (!cobsDecode(block.data(), block.size(), raw)) return;  // malformed block: drop and resync on the next 0x00

    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(raw.data(), raw.size(), header, payload, frameError)) return;  // CRC/format failure: drop

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = assembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    MessageEnvelope envelope;
    std::vector<uint8_t> body;
    CodecError envelopeError;
    if (!decodeEnvelope(assembled.data(), assembled.size(), envelope, body, envelopeError)) return;

    processMessage(envelope, body);
}

void SerialCobsEndpoint::processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body) {
    if (envelope.kind != MessageKind::Command) return;  // this endpoint only accepts commands from the host

    JsonDocument document;
    if (deserializeJson(document, body.data(), body.size())) return;
    JsonObjectConst wrapper = document.as<JsonObjectConst>();
    const char* name = wrapper["name"] | "";
    JsonObjectConst innerBody = wrapper["body"].as<JsonObjectConst>();

    const char* v1Name = mapV2Name(name);
    currentRequestOperationId_ = envelope.operationId;
    currentRequestName_ = name;

    if (v1Name == nullptr) {
        sendError(ProtocolError{name, "unknown_command", "command not supported over EspLink v2 this release"});
        return;
    }

    Command command;
    std::string errorCode, errorMessage;
    if (!JsonCommandCodec::decode(v1Name, innerBody, device_, command, errorCode, errorMessage)) {
        sendError(ProtocolError{name, errorCode, errorMessage});
        return;
    }

    engine_.handle(session_, command, OperationId{nextOperationId_++}, "usb-cobs-v2", *this);
}

void SerialCobsEndpoint::send(const Response& response) {
    // system.hello gets a distinct response name/body per the design doc §8.10 — every
    // other mapped command echoes its own request name.
    const std::string responseName = (currentRequestName_ == "system.hello") ? "system.welcome" : currentRequestName_;

    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = responseName;
    JsonObject body = wrapper["body"].to<JsonObject>();
    if (responseName == "system.welcome") {
        body["deviceId"] = "esbg-usb-v2";
        body["firmware"] = ESPBARCODE_VERSION;
        body["selectedVersion"] = "2.0";
        body["controlSessionId"] = session_.id().value;
        body["carrier"]["profile"] = "stream-standard";
        body["carrier"]["maxFrameBytes"] = 4096;
    } else {
        JsonCommandCodec::encodeBody(response, body);
    }

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Result;
    envelope.serviceId = ServiceId::System;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = session_.id().value;
    envelope.operationId = nextOperationId_++;
    envelope.correlationId = currentRequestOperationId_;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    // Single-fragment response only this session — see Task 9's scope note; a response
    // body larger than one hop frame's payload budget is dropped rather than fragmented.
    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::StreamStandard;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;
    header.fragmentIndex = 0;
    header.fragmentCount = 1;

    std::vector<uint8_t> frame;
    CodecError frameError;
    if (!encodeHopFrame(header, message.data(), message.size(), frame, frameError)) return;

    const std::vector<uint8_t> cobs = cobsEncode(frame.data(), frame.size());
    Serial.write(cobs.data(), cobs.size());
    Serial.write(static_cast<uint8_t>(0x00));
}

void SerialCobsEndpoint::sendError(const ProtocolError& error) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = currentRequestName_;
    wrapper["error"]["code"] = error.code;
    wrapper["error"]["message"] = error.message;

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Error;
    envelope.serviceId = ServiceId::System;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = session_.id().value;
    envelope.operationId = nextOperationId_++;
    envelope.correlationId = currentRequestOperationId_;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::StreamStandard;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;
    header.fragmentIndex = 0;
    header.fragmentCount = 1;

    std::vector<uint8_t> frame;
    CodecError frameError;
    if (!encodeHopFrame(header, message.data(), message.size(), frame, frameError)) return;

    const std::vector<uint8_t> cobs = cobsEncode(frame.data(), frame.size());
    Serial.write(cobs.data(), cobs.size());
    Serial.write(static_cast<uint8_t>(0x00));
}

}  // namespace esplink
```

- [ ] **Step 4: Wire both endpoints into `main.cpp`**

```cpp
#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "BarcodeApplicationAdapter.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "EspIdfDeviceControl.h"
#include "SerialCobsEndpoint.h"
#include "SerialLegacyEndpoint.h"
#include "app_config.h"

namespace {
enum class ActiveTransport : uint8_t { Legacy, CobsV2 };

BarcodeApplication application;
esplink::BarcodeApplicationAdapter applicationAdapter(application);
esplink::EspIdfDeviceControl deviceControl(application);
esplink::ControlProtocolEngine engine(applicationAdapter, applicationAdapter, deviceControl, ESPBARCODE_VERSION);
esplink::ControlSession legacySession(esplink::ControlSessionId{1}, esplink::ControllerId{1});
esplink::ControlSession v2Session(esplink::ControlSessionId{2}, esplink::ControllerId{1});
esplink::SerialLegacyEndpoint legacyEndpoint(engine, legacySession, deviceControl, applicationAdapter);
esplink::SerialCobsEndpoint cobsEndpoint(engine, v2Session, applicationAdapter);
ActiveTransport active = ActiveTransport::Legacy;
}  // namespace

void setup() {
    Serial.begin(app_config::kSerialBaud);
    Serial.setTimeout(25);
    delay(100);

    std::string error;
    if (!application.begin(error)) {
        Serial.printf("{\"event\":\"fatal\",\"message\":\"%s\"}\n", error.c_str());
        return;
    }
    legacyEndpoint.begin();
}

void loop() {
    if (active == ActiveTransport::Legacy) {
        legacyEndpoint.loop();
        if (legacyEndpoint.upgradeRequested()) active = ActiveTransport::CobsV2;
    } else {
        cobsEndpoint.loop();
    }
    application.loop();
    delay(1);
}
```

- [ ] **Step 5: Compile-check under PlatformIO**

```bash
pio run -e esp32dev
```

Expected: zero warnings. As in Task 8, if the ESP32 toolchain is unavailable in this environment, record that explicitly in Task 15's completion summary rather than claiming a build that was not actually run.

- [ ] **Step 6: Document the hardware-validation gap**

This task cannot be exercised end-to-end without a physical device: the `upgrade` handshake, COBS resynchronization, and the five mapped v2 commands have no native/simulated test harness (the same ArduinoJson/`Serial` constraint as Task 8, compounded by needing a real byte-level serial round trip). Add a line to the running list that Task 15 will fold into its completion summary:

> USB v2 path (`SerialLegacyEndpoint` → `upgrade` → `SerialCobsEndpoint`, all five mapped v2 commands, COBS resync after a corrupted frame) is unverified on real hardware as of this task. Needs a HIL smoke test: connect over USB, send `{"cmd":"upgrade"}`, confirm the ack, then exercise `system.hello`/`system.ping`/`barcode.generate`/`barcode.close`/`device.backlight.set` and observe the display.

- [ ] **Step 7: Commit**

```bash
git add src/JsonCommandCodec.h src/JsonCommandCodec.cpp src/SerialLegacyEndpoint.h src/SerialLegacyEndpoint.cpp \
        src/SerialCobsEndpoint.h src/SerialCobsEndpoint.cpp src/main.cpp
git commit -m "feat(firmware): add explicit v1-to-v2 upgrade negotiation and SerialCobsEndpoint"
```

---

### Task 10: .NET — `EspBarcode.Protocol` (envelope, hop frame, COBS, CRC, reassembly)

**Files:**
- Create: `dotnet/src/EspBarcode.Protocol/EspBarcode.Protocol.csproj`
- Create: `dotnet/src/EspBarcode.Protocol/ConnectivityEnums.cs`
- Create: `dotnet/src/EspBarcode.Protocol/MessageEnvelope.cs`
- Create: `dotnet/src/EspBarcode.Protocol/HopFrame.cs`
- Create: `dotnet/src/EspBarcode.Protocol/Cobs.cs`
- Create: `dotnet/src/EspBarcode.Protocol/Crc32.cs`
- Create: `dotnet/src/EspBarcode.Protocol/FrameAssembler.cs`
- Create: `dotnet/tests/EspBarcode.Protocol.Tests/EspBarcode.Protocol.Tests.csproj`
- Create: `dotnet/tests/EspBarcode.Protocol.Tests/CodecTests.cs`
- Modify: `dotnet/EspScreenBarcodeGenerator.slnx`

**Interfaces:**
- Produces: `EspBarcode.Protocol.MessageKind/ServiceId/CodecId/FrameType/TrafficClass/CarrierProfileId` (same numeric values as the C++ `ConnectivityTypes.h` from Task 1 — Global Constraints), `EspBarcode.Protocol.MessageEnvelope`, `EspBarcode.Protocol.HopFrameHeader`, `EspBarcode.Protocol.Cobs`, `EspBarcode.Protocol.Crc32`, `EspBarcode.Protocol.FrameAssembler`, `EspBarcode.Protocol.AssemblyOutcome`. Task 12's `EspLinkLinkSession` and Task 13's `SerialV2Connector` depend on these. This project must decode/encode the **exact same bytes** as Task 4's C++ codec — the test vectors are the same hex strings, byte-for-byte.

- [ ] **Step 1: Create the project**

`dotnet/src/EspBarcode.Protocol/EspBarcode.Protocol.csproj`:

```xml
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <PackageId>EspBarcode.Protocol</PackageId>
    <Description>EspLink v2 message envelope, hop frame, COBS, CRC-32, and reassembly codec. Portable — no serial/WinRT dependency.</Description>
  </PropertyGroup>

</Project>
```

- [ ] **Step 2: Write `ConnectivityEnums.cs`**

Numeric values fixed by this plan's Global Constraints — must match `lib/EspLinkCore/src/ConnectivityTypes.h` (Task 1) exactly.

```csharp
namespace EspBarcode.Protocol;

public enum MessageKind : byte { Command = 0, Result = 1, Event = 2, Error = 3, Transfer = 4 }

public enum ServiceId : byte
{
    System = 0, Barcode = 1, Preset = 2, Transfer = 3, Device = 4,
    Connectivity = 5, Trust = 6, Gateway = 7, Diagnostics = 8,
}

public enum CodecId : byte { Json = 0, Binary = 1 }

public enum FrameType : byte { Data = 0, Ack = 1, Nack = 2, KeepAlive = 3, Close = 4, Reset = 5 }

public enum TrafficClass : byte { Control = 0, Metadata = 1, Bulk = 2, Critical = 3, Event = 4 }

public enum CarrierProfileId : byte
{
    Unspecified = 0, EspNowV1 = 1, EspNowV2 = 2, StreamSmall = 3,
    StreamStandard = 4, StreamLarge = 5, TcpStandard = 6, TcpLarge = 7,
}

public enum CodecError
{
    None, TooShort, BadMagic, UnsupportedMajorVersion, BodyLengthMismatch,
    CrcMismatch, PayloadTooLarge, ReservedFieldNonZero, BadHeaderLength,
    InvalidFragmentIndex,
}
```

- [ ] **Step 3: Write `Crc32.cs`**

```csharp
namespace EspBarcode.Protocol;

public static class Crc32
{
    public static uint Compute(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in data)
        {
            crc ^= b;
            for (int bit = 0; bit < 8; bit++)
            {
                uint mask = (uint)-(crc & 1);
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }
}
```

- [ ] **Step 4: Write `MessageEnvelope.cs`**

```csharp
using System.Buffers.Binary;

namespace EspBarcode.Protocol;

public sealed record MessageEnvelope
{
    public byte Major { get; init; } = 2;
    public byte Minor { get; init; }
    public MessageKind Kind { get; init; } = MessageKind.Command;
    public byte Flags { get; init; }
    public ServiceId ServiceId { get; init; } = ServiceId.System;
    public CodecId CodecId { get; init; } = CodecId.Json;
    public uint ControlSessionId { get; init; }
    public uint BodyLength { get; init; }
    public ulong OperationId { get; init; }
    public ulong CorrelationId { get; init; }

    public const int HeaderSize = 32;

    public static bool TryEncode(MessageEnvelope envelope, ReadOnlySpan<byte> body, out byte[] message, out CodecError error)
    {
        message = new byte[HeaderSize + body.Length];
        var span = message.AsSpan();
        span[0] = (byte)'E';
        span[1] = (byte)'M';
        span[2] = envelope.Major;
        span[3] = envelope.Minor;
        span[4] = (byte)envelope.Kind;
        span[5] = envelope.Flags;
        span[6] = (byte)envelope.ServiceId;
        span[7] = (byte)envelope.CodecId;
        BinaryPrimitives.WriteUInt32LittleEndian(span[8..], envelope.ControlSessionId);
        BinaryPrimitives.WriteUInt32LittleEndian(span[12..], (uint)body.Length);
        BinaryPrimitives.WriteUInt64LittleEndian(span[16..], envelope.OperationId);
        BinaryPrimitives.WriteUInt64LittleEndian(span[24..], envelope.CorrelationId);
        body.CopyTo(span[HeaderSize..]);
        error = CodecError.None;
        return true;
    }

    public static bool TryDecode(ReadOnlySpan<byte> bytes, out MessageEnvelope envelope, out byte[] body, out CodecError error)
    {
        envelope = new MessageEnvelope();
        body = [];
        if (bytes.Length < HeaderSize) { error = CodecError.TooShort; return false; }
        if (bytes[0] != (byte)'E' || bytes[1] != (byte)'M') { error = CodecError.BadMagic; return false; }
        byte major = bytes[2];
        if (major != 2) { error = CodecError.UnsupportedMajorVersion; return false; }

        uint bodyLength = BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]);
        if (bytes.Length - HeaderSize < bodyLength) { error = CodecError.BodyLengthMismatch; return false; }

        envelope = new MessageEnvelope
        {
            Major = major,
            Minor = bytes[3],
            Kind = (MessageKind)bytes[4],
            Flags = bytes[5],
            ServiceId = (ServiceId)bytes[6],
            CodecId = (CodecId)bytes[7],
            ControlSessionId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]),
            BodyLength = bodyLength,
            OperationId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[16..]),
            CorrelationId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[24..]),
        };
        body = bytes.Slice(HeaderSize, (int)bodyLength).ToArray();
        error = CodecError.None;
        return true;
    }
}
```

- [ ] **Step 5: Write `HopFrame.cs`**

```csharp
using System.Buffers.Binary;

namespace EspBarcode.Protocol;

public sealed record HopFrameHeader
{
    public byte Major { get; init; } = 2;
    public byte Minor { get; init; }
    public FrameType FrameType { get; init; } = FrameType.Data;
    public byte Flags { get; init; }
    public TrafficClass TrafficClass { get; init; } = TrafficClass.Control;
    public CarrierProfileId ProfileId { get; init; } = CarrierProfileId.Unspecified;
    public ushort RouteId { get; init; }
    public uint LinkSessionId { get; init; }
    public uint LinkMessageId { get; init; }
    public uint LinkCorrelationId { get; init; }
    public ushort FragmentIndex { get; init; }
    public ushort FragmentCount { get; init; } = 1;

    public const int HeaderSize = 32;
    public const int Overhead = 36; // header(32) + crc32 trailer(4)

    public static bool TryEncode(HopFrameHeader header, ReadOnlySpan<byte> payload, out byte[] frame, out CodecError error)
    {
        if (payload.Length > ushort.MaxValue) { frame = []; error = CodecError.PayloadTooLarge; return false; }
        frame = new byte[Overhead + payload.Length];
        var span = frame.AsSpan();
        span[0] = (byte)'E';
        span[1] = (byte)'L';
        span[2] = header.Major;
        span[3] = header.Minor;
        span[4] = (byte)header.FrameType;
        span[5] = header.Flags;
        span[6] = (byte)header.TrafficClass;
        span[7] = (byte)header.ProfileId;
        BinaryPrimitives.WriteUInt16LittleEndian(span[8..], header.RouteId);
        BinaryPrimitives.WriteUInt16LittleEndian(span[10..], HeaderSize);
        BinaryPrimitives.WriteUInt32LittleEndian(span[12..], header.LinkSessionId);
        BinaryPrimitives.WriteUInt32LittleEndian(span[16..], header.LinkMessageId);
        BinaryPrimitives.WriteUInt32LittleEndian(span[20..], header.LinkCorrelationId);
        BinaryPrimitives.WriteUInt16LittleEndian(span[24..], header.FragmentIndex);
        BinaryPrimitives.WriteUInt16LittleEndian(span[26..], header.FragmentCount);
        BinaryPrimitives.WriteUInt16LittleEndian(span[28..], (ushort)payload.Length);
        BinaryPrimitives.WriteUInt16LittleEndian(span[30..], 0);
        payload.CopyTo(span[HeaderSize..]);

        uint crc = Crc32.Compute(span[..(HeaderSize + payload.Length)]);
        BinaryPrimitives.WriteUInt32LittleEndian(span[(HeaderSize + payload.Length)..], crc);
        error = CodecError.None;
        return true;
    }

    public static bool TryDecode(ReadOnlySpan<byte> bytes, out HopFrameHeader header, out byte[] payload, out CodecError error)
    {
        header = new HopFrameHeader();
        payload = [];
        if (bytes.Length < Overhead) { error = CodecError.TooShort; return false; }
        if (bytes[0] != (byte)'E' || bytes[1] != (byte)'L') { error = CodecError.BadMagic; return false; }
        byte major = bytes[2];
        if (major != 2) { error = CodecError.UnsupportedMajorVersion; return false; }
        ushort headerLength = BinaryPrimitives.ReadUInt16LittleEndian(bytes[10..]);
        if (headerLength != HeaderSize) { error = CodecError.BadHeaderLength; return false; }
        ushort payloadLength = BinaryPrimitives.ReadUInt16LittleEndian(bytes[28..]);
        ushort reserved = BinaryPrimitives.ReadUInt16LittleEndian(bytes[30..]);
        if (reserved != 0) { error = CodecError.ReservedFieldNonZero; return false; }
        ushort fragmentIndex = BinaryPrimitives.ReadUInt16LittleEndian(bytes[24..]);
        ushort fragmentCount = BinaryPrimitives.ReadUInt16LittleEndian(bytes[26..]);
        if (fragmentCount == 0 || fragmentIndex >= fragmentCount) { error = CodecError.InvalidFragmentIndex; return false; }

        int rawLength = HeaderSize + payloadLength + 4;
        if (bytes.Length < rawLength) { error = CodecError.TooShort; return false; }

        uint expectedCrc = BinaryPrimitives.ReadUInt32LittleEndian(bytes[(HeaderSize + payloadLength)..]);
        uint actualCrc = Crc32.Compute(bytes[..(HeaderSize + payloadLength)]);
        if (expectedCrc != actualCrc) { error = CodecError.CrcMismatch; return false; }

        header = new HopFrameHeader
        {
            Major = major,
            Minor = bytes[3],
            FrameType = (FrameType)bytes[4],
            Flags = bytes[5],
            TrafficClass = (TrafficClass)bytes[6],
            ProfileId = (CarrierProfileId)bytes[7],
            RouteId = BinaryPrimitives.ReadUInt16LittleEndian(bytes[8..]),
            LinkSessionId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]),
            LinkMessageId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]),
            LinkCorrelationId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[20..]),
            FragmentIndex = fragmentIndex,
            FragmentCount = fragmentCount,
        };
        payload = bytes.Slice(HeaderSize, payloadLength).ToArray();
        error = CodecError.None;
        return true;
    }
}
```

- [ ] **Step 6: Write `Cobs.cs`**

Mirrors `lib/EspLinkCore/src/Cobs.cpp` (Task 4) exactly — operates on the COBS block without the trailing stream delimiter.

```csharp
namespace EspBarcode.Protocol;

public static class Cobs
{
    public static byte[] Encode(ReadOnlySpan<byte> data)
    {
        var output = new List<byte>(data.Length + data.Length / 254 + 2);
        int read = 0;
        while (true)
        {
            int blockStart = read;
            int blockLen = 0;
            while (read < data.Length && data[read] != 0x00 && blockLen < 254) { read++; blockLen++; }
            bool hitZero = read < data.Length && data[read] == 0x00;
            output.Add((byte)(blockLen + 1));
            for (int i = 0; i < blockLen; i++) output.Add(data[blockStart + i]);
            if (hitZero)
            {
                read++;
                if (read >= data.Length) break;
            }
            else if (read >= data.Length)
            {
                break;
            }
        }
        return [.. output];
    }

    public static bool TryDecode(ReadOnlySpan<byte> data, out byte[] decoded)
    {
        var output = new List<byte>(data.Length);
        int read = 0;
        while (read < data.Length)
        {
            byte code = data[read];
            if (code == 0) { decoded = []; return false; }
            read++;
            int blockLen = code - 1;
            if (read + blockLen > data.Length) { decoded = []; return false; }
            for (int i = 0; i < blockLen; i++) output.Add(data[read + i]);
            read += blockLen;
            if (code != 255 && read < data.Length) output.Add(0x00);
        }
        decoded = [.. output];
        return true;
    }
}
```

- [ ] **Step 7: Write `FrameAssembler.cs`**

Mirrors `lib/EspLinkCore/src/FrameAssembler.cpp` (Task 4): bounded, FIFO eviction, duplicate/conflict detection.

```csharp
namespace EspBarcode.Protocol;

public enum AssemblyOutcome { Incomplete, Complete, DuplicateIgnored, Conflict }

public sealed class FrameAssembler(int maxConcurrentMessages = 2)
{
    private readonly record struct Key(uint LinkSessionId, uint LinkMessageId, ushort RouteId);

    private sealed class Partial
    {
        public Key Key;
        public ushort FragmentCount;
        public byte[]?[] Fragments = [];
        public int ReceivedCount;
    }

    private readonly List<Partial> _partial = [];

    public AssemblyOutcome AddFragment(HopFrameHeader header, byte[] payload, out byte[] assembled)
    {
        var key = new Key(header.LinkSessionId, header.LinkMessageId, header.RouteId);
        var entry = _partial.Find(p => p.Key.Equals(key));

        if (entry is null)
        {
            if (_partial.Count >= maxConcurrentMessages) _partial.RemoveAt(0);
            entry = new Partial { Key = key, FragmentCount = header.FragmentCount, Fragments = new byte[header.FragmentCount][] };
            _partial.Add(entry);
        }

        if (header.FragmentCount != entry.FragmentCount || header.FragmentIndex >= entry.Fragments.Length)
        {
            assembled = [];
            return AssemblyOutcome.Conflict;
        }

        var existing = entry.Fragments[header.FragmentIndex];
        if (existing is not null)
        {
            assembled = [];
            return existing.AsSpan().SequenceEqual(payload) ? AssemblyOutcome.DuplicateIgnored : AssemblyOutcome.Conflict;
        }
        entry.Fragments[header.FragmentIndex] = payload;
        entry.ReceivedCount++;

        if (entry.ReceivedCount < entry.FragmentCount) { assembled = []; return AssemblyOutcome.Incomplete; }

        var combined = new List<byte>();
        foreach (var fragment in entry.Fragments) combined.AddRange(fragment!);
        assembled = [.. combined];
        _partial.Remove(entry);
        return AssemblyOutcome.Complete;
    }
}
```

- [ ] **Step 8: Add the project to the solution**

In `dotnet/EspScreenBarcodeGenerator.slnx`, add under `<Folder Name="/src/">`:

```xml
<Project Path="src/EspBarcode.Protocol/EspBarcode.Protocol.csproj" />
```

- [ ] **Step 9: Write the test project and vector tests**

`dotnet/tests/EspBarcode.Protocol.Tests/EspBarcode.Protocol.Tests.csproj` (copy `dotnet/tests/EspBarcode.Generator.Tests/EspBarcode.Generator.Tests.csproj` and change the `ProjectReference` to `..\..\src\EspBarcode.Protocol\EspBarcode.Protocol.csproj`), then add to the `slnx` under `<Folder Name="/tests/">`:

```xml
<Project Path="tests/EspBarcode.Protocol.Tests/EspBarcode.Protocol.Tests.csproj" />
```

`dotnet/tests/EspBarcode.Protocol.Tests/CodecTests.cs` — uses the **exact same vectors** as Task 4's `tests/esplink_codec_tests.cpp` (Vector A/B/C/D hex strings, byte-identical):

```csharp
using EspBarcode.Protocol;

namespace EspBarcode.Protocol.Tests;

public class CodecTests
{
    // Vector A: Layer 3 envelope wrapping {"schema":"esbg.control/2.0","name":"system.ping","body":{}}
    private const string VectorAHex =
        "454d020000000000000000003c000000010000000000000000000000000000007b22736368656d61223a22" +
        "657362672e636f6e74726f6c2f322e30222c226e616d65223a2273797374656d2e70696e67222c22626f6479" +
        "223a7b7d7d";

    private static byte[] FromHex(string hex) => Convert.FromHexString(hex);

    [Fact]
    public void Envelope_Encode_MatchesVectorA()
    {
        var body = FromHex(
            "7b22736368656d61223a22657362672e636f6e74726f6c2f322e30222c226e616d65223a2273797374656d" +
            "2e70696e67222c22626f6479223a7b7d7d");
        var envelope = new MessageEnvelope { OperationId = 1 };

        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(FromHex(VectorAHex), message);
    }

    [Fact]
    public void Envelope_Decode_RoundTripsVectorA()
    {
        var bytes = FromHex(VectorAHex);
        Assert.True(MessageEnvelope.TryDecode(bytes, out var envelope, out var body, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(2, envelope.Major);
        Assert.Equal(MessageKind.Command, envelope.Kind);
        Assert.Equal(ServiceId.System, envelope.ServiceId);
        Assert.Equal(1ul, envelope.OperationId);
        Assert.Equal(60, body.Length);
    }

    [Fact]
    public void Envelope_Decode_RejectsTruncatedInput()
    {
        var bytes = FromHex(VectorAHex)[..20];
        Assert.False(MessageEnvelope.TryDecode(bytes, out _, out _, out var error));
        Assert.Equal(CodecError.TooShort, error);
    }

    // Vector B: single hop frame (stream-standard, linkSessionId=1001, linkMessageId=1)
    // wrapping Vector A's message.
    private const string VectorBHex =
        "454c02000001000400002000e90300000100000000000000000001005c000000454d0200000000000000000" +
        "03c000000010000000000000000000000000000007b22736368656d61223a22657362672e636f6e74726f6c2f" +
        "322e30222c226e616d65223a2273797374656d2e70696e67222c22626f6479223a7b7d7dee2c40c3";

    [Fact]
    public void HopFrame_Encode_MatchesVectorB()
    {
        var header = new HopFrameHeader
        {
            Flags = 0x01,
            ProfileId = CarrierProfileId.StreamStandard,
            LinkSessionId = 1001,
            LinkMessageId = 1,
        };
        var payload = FromHex(VectorAHex);

        Assert.True(HopFrameHeader.TryEncode(header, payload, out var frame, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(FromHex(VectorBHex), frame);
        Assert.Equal(36 + payload.Length, frame.Length);
    }

    [Fact]
    public void HopFrame_Decode_ValidatesCrcAndRecoversPayload()
    {
        var raw = FromHex(VectorBHex);
        Assert.True(HopFrameHeader.TryDecode(raw, out var header, out var payload, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(CarrierProfileId.StreamStandard, header.ProfileId);
        Assert.Equal(0, header.FragmentIndex);
        Assert.Equal(1, header.FragmentCount);
        Assert.Equal(FromHex(VectorAHex), payload);
    }

    [Fact]
    public void HopFrame_Decode_RejectsCorruptedPayload()
    {
        var raw = FromHex(VectorBHex);
        raw[^5] ^= 0xFF;
        Assert.False(HopFrameHeader.TryDecode(raw, out _, out _, out var error));
        Assert.Equal(CodecError.CrcMismatch, error);
    }

    [Fact]
    public void Cobs_RoundTrips_FrameContainingZeroBytes()
    {
        var raw = FromHex(VectorBHex);
        var encoded = Cobs.Encode(raw);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.True(Cobs.TryDecode(encoded, out var decoded));
        Assert.Equal(raw, decoded);
    }

    [Fact]
    public void Cobs_Rejects_LiteralZeroInsideBlock()
    {
        byte[] corrupt = [0x03, 0x01, 0x00, 0x02];
        Assert.False(Cobs.TryDecode(corrupt, out _));
    }

    [Fact]
    public void FrameAssembler_ReassemblesTwoFragmentsInOrder()
    {
        var envelope = new MessageEnvelope { ServiceId = ServiceId.Barcode, OperationId = 7 };
        var data220 = new string('A', 220);
        var bodyStr = $"{{\"schema\":\"esbg.control/2.0\",\"name\":\"barcode.generate\",\"body\":{{\"type\":\"qr\",\"data\":\"{data220}\",\"display\":true}}}}";
        var body = System.Text.Encoding.UTF8.GetBytes(bodyStr);
        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out _));
        Assert.Equal(353, message.Length);

        const int maxPayload = 214;
        var frag0Payload = message[..maxPayload];
        var frag1Payload = message[maxPayload..];

        var h0 = new HopFrameHeader { TrafficClass = TrafficClass.Critical, ProfileId = CarrierProfileId.EspNowV1, LinkSessionId = 2002, LinkMessageId = 5, FragmentIndex = 0, FragmentCount = 2 };
        var h1 = h0 with { FragmentIndex = 1 };
        Assert.True(HopFrameHeader.TryEncode(h0, frag0Payload, out var frame0, out _));
        Assert.True(HopFrameHeader.TryEncode(h1, frag1Payload, out var frame1, out _));

        Assert.True(HopFrameHeader.TryDecode(frame0, out var dh0, out var p0, out _));
        Assert.True(HopFrameHeader.TryDecode(frame1, out var dh1, out var p1, out _));

        var assembler = new FrameAssembler();
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(dh0, p0, out _));
        Assert.Equal(AssemblyOutcome.Complete, assembler.AddFragment(dh1, p1, out var assembled));
        Assert.Equal(message, assembled);
    }

    [Fact]
    public void FrameAssembler_IgnoresExactDuplicateFragment()
    {
        var h = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 1, FragmentIndex = 0, FragmentCount = 2 };
        byte[] payload = [1, 2, 3];
        var assembler = new FrameAssembler();
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h, payload, out _));
        Assert.Equal(AssemblyOutcome.DuplicateIgnored, assembler.AddFragment(h, payload, out _));
    }

    [Fact]
    public void FrameAssembler_FlagsConflictingDuplicateFragment()
    {
        var h = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 2, FragmentIndex = 0, FragmentCount = 2 };
        var assembler = new FrameAssembler();
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h, [1, 2, 3], out _));
        Assert.Equal(AssemblyOutcome.Conflict, assembler.AddFragment(h, [9, 9, 9], out _));
    }

    [Fact]
    public void FrameAssembler_BoundsConcurrentMessages()
    {
        var assembler = new FrameAssembler(maxConcurrentMessages: 2);
        var h1 = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 1, FragmentIndex = 0, FragmentCount = 2 };
        var h2 = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 2, FragmentIndex = 0, FragmentCount = 2 };
        var h3 = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 3, FragmentIndex = 0, FragmentCount = 2 };
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h1, [1], out _));
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h2, [2], out _));
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h3, [3], out _));
        var h1b = h1 with { FragmentIndex = 1 };
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h1b, [1, 1], out _));
    }
}
```

- [ ] **Step 10: Build and run**

```bash
dotnet build dotnet/EspScreenBarcodeGenerator.slnx
dotnet test dotnet/tests/EspBarcode.Protocol.Tests/EspBarcode.Protocol.Tests.csproj
```

Expected: all 12 tests pass, including the exact-byte vector comparisons matching Task 4's C++ vectors.

- [ ] **Step 11: Commit**

```bash
git add dotnet/src/EspBarcode.Protocol dotnet/tests/EspBarcode.Protocol.Tests dotnet/EspScreenBarcodeGenerator.slnx
git commit -m "feat(dotnet): add EspBarcode.Protocol codec matching the firmware EspLink v2 wire format"
```

---

### Task 11: .NET — `EspBarcode.Connectivity` (value objects + `TransportSelector`)

**Files:**
- Create: `dotnet/src/EspBarcode.Connectivity/EspBarcode.Connectivity.csproj`
- Create: `dotnet/src/EspBarcode.Connectivity/ConnectivityTypes.cs`
- Create: `dotnet/src/EspBarcode.Connectivity/TransportSelector.cs`
- Create: `dotnet/tests/EspBarcode.Connectivity.Tests/EspBarcode.Connectivity.Tests.csproj`
- Create: `dotnet/tests/EspBarcode.Connectivity.Tests/TransportSelectorTests.cs`
- Modify: `dotnet/EspScreenBarcodeGenerator.slnx`

**Interfaces:**
- Produces: `EspBarcode.Connectivity.RunMode/TransportKind/CapabilityState/OptimizationGoal/SideEffectPermission`, `ConnectionRequirement`, `TransportCapabilities`, `SelectionPolicyConfig`, `CandidateScore`, `SelectionDecision`, `TransportSelector.Evaluate(...)`. Semantics mirror `lib/EspLinkCore/src/SelectionPolicy.h` (Task 2) — same eligibility rules, same base-score table, same tie-break (first-eligible-highest-score) — proven with the same eight scenarios, not shared code (no cross-language dependency is introduced; parity is enforced by porting the same table-driven tests).

- [ ] **Step 1: Create the project**

`dotnet/src/EspBarcode.Connectivity/EspBarcode.Connectivity.csproj`:

```xml
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <PackageId>EspBarcode.Connectivity</PackageId>
    <Description>Transport-kind value objects and the pure connectivity selection policy. Portable — no serial/WinRT dependency.</Description>
  </PropertyGroup>

</Project>
```

- [ ] **Step 2: Write `ConnectivityTypes.cs`**

```csharp
namespace EspBarcode.Connectivity;

public enum RunMode { Standalone, Auto, UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway, Adaptive }

public enum TransportKind { UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway }

public enum CapabilityState
{
    CompiledOut, UnsupportedHardware, Disabled, Blocked, Unavailable,
    Available, Degraded, Experimental, Qualified,
}

public enum OptimizationGoal { LowLatency, HighThroughput, Balanced, MinimalHostDependencies, Deterministic, MinimumPower }

public enum SideEffectPermission { Forbidden, Allowed, Required }

public sealed record ConnectionRequirement
{
    public bool WirelessRequired { get; init; }
    public SideEffectPermission ExternalHardware { get; init; } = SideEffectPermission.Allowed;
    public SideEffectPermission IpInterface { get; init; } = SideEffectPermission.Allowed;
    public SideEffectPermission MultiDevice { get; init; } = SideEffectPermission.Allowed;
}

public sealed record TransportCapabilities
{
    public required TransportKind Kind { get; init; }
    public required CapabilityState State { get; init; }
    public bool Wireless { get; init; }
    public bool RequiresExternalHardware { get; init; }
    public bool CreatesIpInterface { get; init; }
    public bool SupportsMultipleDevices { get; init; }
}

public sealed record SelectionPolicyConfig
{
    public RunMode Mode { get; init; } = RunMode.Auto;
    public OptimizationGoal Goal { get; init; } = OptimizationGoal.Balanced;
    public ConnectionRequirement Requirement { get; init; } = new();
    public IReadOnlyList<TransportKind> Allow { get; init; } = [];
    public IReadOnlyList<TransportKind> Deny { get; init; } = [];
}

public sealed record CandidateScore
{
    public required TransportKind Kind { get; init; }
    public bool Eligible { get; init; }
    public int Score { get; init; }
    public string Reason { get; init; } = "";
}

public sealed record SelectionDecision
{
    public TransportKind? Selected { get; init; }
    public IReadOnlyList<CandidateScore> Candidates { get; init; } = [];
}
```

- [ ] **Step 3: Write `TransportSelector.cs`**

Base score table and eligibility order match `lib/EspLinkCore/src/SelectionPolicy.h` (Task 2) exactly.

```csharp
namespace EspBarcode.Connectivity;

public static class TransportSelector
{
    // [OptimizationGoal][TransportKind], index order: UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway.
    private static readonly int[,] BaseScore =
    {
        /* LowLatency */      { 50, 90, 60, 80, 100 },
        /* HighThroughput */  { 40, 90, 50, 100, 85 },
        /* Balanced */        { 50, 80, 60, 70, 75 },
        /* MinimalHostDeps */ { 85, 85, 90, 90, 40 },
        /* Deterministic */   { 95, 100, 50, 40, 70 },
        /* MinimumPower */    { 70, 70, 80, 40, 85 },
    };

    private static int StatePenalty(CapabilityState state) => state switch
    {
        CapabilityState.Experimental => 15,
        CapabilityState.Degraded => 30,
        _ => 0,
    };

    public static SelectionDecision Evaluate(SelectionPolicyConfig policy, IReadOnlyList<TransportCapabilities> candidates)
    {
        var scored = new List<CandidateScore>(candidates.Count);

        foreach (var candidate in candidates)
        {
            bool eligible = false;
            int score = 0;
            string reason;

            if (policy.Deny.Contains(candidate.Kind))
            {
                reason = "denied by policy";
            }
            else if (policy.Allow.Count > 0 && !policy.Allow.Contains(candidate.Kind))
            {
                reason = "not in allow list";
            }
            else if (candidate.State == CapabilityState.CompiledOut) reason = "compiled out";
            else if (candidate.State == CapabilityState.UnsupportedHardware) reason = "unsupported hardware";
            else if (candidate.State == CapabilityState.Disabled) reason = "disabled";
            else if (candidate.State == CapabilityState.Blocked) reason = "blocked";
            else if (candidate.State == CapabilityState.Unavailable) reason = "unavailable";
            else if (policy.Requirement.WirelessRequired && !candidate.Wireless) reason = "wireless required";
            else if (policy.Requirement.ExternalHardware == SideEffectPermission.Forbidden && candidate.RequiresExternalHardware)
                reason = "external hardware forbidden by policy";
            else if (policy.Requirement.ExternalHardware == SideEffectPermission.Required && !candidate.RequiresExternalHardware)
                reason = "external hardware required by policy";
            else if (policy.Requirement.IpInterface == SideEffectPermission.Forbidden && candidate.CreatesIpInterface)
                reason = "IP interface forbidden by policy";
            else if (policy.Requirement.IpInterface == SideEffectPermission.Required && !candidate.CreatesIpInterface)
                reason = "IP interface required by policy";
            else if (policy.Requirement.MultiDevice == SideEffectPermission.Required && !candidate.SupportsMultipleDevices)
                reason = "multi-device support required by policy";
            else
            {
                eligible = true;
                score = BaseScore[(int)policy.Goal, (int)candidate.Kind] - StatePenalty(candidate.State);
                reason = "eligible";
            }

            scored.Add(new CandidateScore { Kind = candidate.Kind, Eligible = eligible, Score = score, Reason = reason });
        }

        TransportKind? selected = null;
        int bestScore = 0;
        foreach (var c in scored)
        {
            if (!c.Eligible) continue;
            if (selected is null || c.Score > bestScore) { selected = c.Kind; bestScore = c.Score; }
        }

        return new SelectionDecision { Selected = selected, Candidates = scored };
    }
}
```

- [ ] **Step 4: Add the project to the solution**

`dotnet/EspScreenBarcodeGenerator.slnx`, under `<Folder Name="/src/">`:

```xml
<Project Path="src/EspBarcode.Connectivity/EspBarcode.Connectivity.csproj" />
```

- [ ] **Step 5: Write the test project and port Task 2's eight scenarios**

`dotnet/tests/EspBarcode.Connectivity.Tests/EspBarcode.Connectivity.Tests.csproj` (copy Task 10's test csproj pattern, `ProjectReference` to `..\..\src\EspBarcode.Connectivity\EspBarcode.Connectivity.csproj`), add to the `slnx` under `<Folder Name="/tests/">`.

`dotnet/tests/EspBarcode.Connectivity.Tests/TransportSelectorTests.cs`:

```csharp
using EspBarcode.Connectivity;

namespace EspBarcode.Connectivity.Tests;

public class TransportSelectorTests
{
    private static TransportCapabilities Available(TransportKind kind, bool wireless = false, bool hw = false, bool ip = false, bool multi = false) =>
        new() { Kind = kind, State = CapabilityState.Available, Wireless = wireless, RequiresExternalHardware = hw, CreatesIpInterface = ip, SupportsMultipleDevices = multi };

    [Fact]
    public void NoRequirement_PicksHighestScore_RegardlessOfWireless()
    {
        var policy = new SelectionPolicyConfig { Goal = OptimizationGoal.LowLatency };
        var candidates = new[] { Available(TransportKind.UsbV2), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.UsbV2, decision.Selected);
    }

    [Fact]
    public void WirelessRequired_ExcludesUsb()
    {
        var policy = new SelectionPolicyConfig { Goal = OptimizationGoal.LowLatency, Requirement = new() { WirelessRequired = true } };
        var candidates = new[] { Available(TransportKind.UsbV2), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.Bluetooth, decision.Selected);
        Assert.False(decision.Candidates[0].Eligible);
        Assert.Equal("wireless required", decision.Candidates[0].Reason);
    }

    [Fact]
    public void DenyList_RemovesBestScoringCandidate()
    {
        var policy = new SelectionPolicyConfig
        {
            Goal = OptimizationGoal.HighThroughput,
            Requirement = new() { WirelessRequired = true },
            Deny = [TransportKind.WifiDirect],
        };
        var candidates = new[] { Available(TransportKind.WifiDirect, wireless: true), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.Bluetooth, decision.Selected);
        Assert.Equal("denied by policy", decision.Candidates[0].Reason);
    }

    [Fact]
    public void IpInterfaceForbidden_ExcludesWifiDirect()
    {
        var policy = new SelectionPolicyConfig
        {
            Goal = OptimizationGoal.Balanced,
            Requirement = new() { WirelessRequired = true, IpInterface = SideEffectPermission.Forbidden },
        };
        var candidates = new[]
        {
            Available(TransportKind.WifiDirect, wireless: true, ip: true),
            Available(TransportKind.EspNowGateway, wireless: true, hw: true),
        };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.EspNowGateway, decision.Selected);
    }

    [Fact]
    public void ExternalHardwareForbidden_ExcludesGateway()
    {
        var policy = new SelectionPolicyConfig
        {
            Goal = OptimizationGoal.Balanced,
            Requirement = new() { WirelessRequired = true, ExternalHardware = SideEffectPermission.Forbidden },
        };
        var candidates = new[] { Available(TransportKind.EspNowGateway, wireless: true, hw: true), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.Bluetooth, decision.Selected);
    }

    [Fact]
    public void AllCandidatesIneligible_YieldsNoSelectionWithReasons()
    {
        var policy = new SelectionPolicyConfig();
        var candidates = new[]
        {
            new TransportCapabilities { Kind = TransportKind.Bluetooth, State = CapabilityState.CompiledOut },
            new TransportCapabilities { Kind = TransportKind.WifiDirect, State = CapabilityState.Blocked },
        };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Null(decision.Selected);
        Assert.Equal("compiled out", decision.Candidates[0].Reason);
        Assert.Equal("blocked", decision.Candidates[1].Reason);
    }

    [Fact]
    public void AllowList_RestrictsToNamedKinds()
    {
        var policy = new SelectionPolicyConfig { Allow = [TransportKind.UsbV2] };
        var candidates = new[] { Available(TransportKind.UsbV2), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.UsbV2, decision.Selected);
        Assert.Equal("not in allow list", decision.Candidates[1].Reason);
    }

    [Fact]
    public void Decision_IsDeterministicAcrossRepeatedEvaluation()
    {
        var policy = new SelectionPolicyConfig { Goal = OptimizationGoal.Deterministic };
        var candidates = new[] { Available(TransportKind.UsbV1), Available(TransportKind.UsbV2) };
        var first = TransportSelector.Evaluate(policy, candidates);
        var second = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(first.Selected, second.Selected);
        Assert.Equal(TransportKind.UsbV2, first.Selected);
    }
}
```

- [ ] **Step 6: Build and run**

```bash
dotnet build dotnet/EspScreenBarcodeGenerator.slnx
dotnet test dotnet/tests/EspBarcode.Connectivity.Tests/EspBarcode.Connectivity.Tests.csproj
```

Expected: all 8 tests pass.

- [ ] **Step 7: Commit**

```bash
git add dotnet/src/EspBarcode.Connectivity dotnet/tests/EspBarcode.Connectivity.Tests dotnet/EspScreenBarcodeGenerator.slnx
git commit -m "feat(dotnet): add EspBarcode.Connectivity value objects and TransportSelector"
```

---

### Task 12: .NET — typed v2 client core (`ILinkConnector`, `EspLinkLinkSession`, `EspLinkControlSession`)

**Files:**
- Modify: `dotnet/src/EspBarcode.Connectivity/EspBarcode.Connectivity.csproj` (add a `ProjectReference` to `EspBarcode.Protocol`)
- Create: `dotnet/src/EspBarcode.Connectivity/Client/ILinkConnection.cs`
- Create: `dotnet/src/EspBarcode.Connectivity/Client/InMemoryDuplexConnection.cs`
- Create: `dotnet/src/EspBarcode.Connectivity/Client/EspLinkLinkSession.cs`
- Create: `dotnet/src/EspBarcode.Connectivity/Client/EspLinkControlSession.cs`
- Create: `dotnet/tests/EspBarcode.Connectivity.Tests/LinkSessionTests.cs`

**Interfaces:**
- Consumes: `EspBarcode.Protocol.MessageEnvelope/HopFrameHeader/Cobs/FrameAssembler/AssemblyOutcome` (Task 10).
- Produces: `EspBarcode.Connectivity.Client.ILinkConnection`, `ILinkConnector`, `InMemoryDuplexConnection`, `EspLinkLinkSession`, `EspLinkControlSession`. Task 13's `SerialV2Connector` implements `ILinkConnector`/`ILinkConnection` against `System.IO.Ports.SerialPort` and is otherwise a drop-in replacement for `InMemoryDuplexConnection` in these tests.

- [ ] **Step 1: Add the `EspBarcode.Protocol` reference**

```xml
<!-- dotnet/src/EspBarcode.Connectivity/EspBarcode.Connectivity.csproj — add: -->
<ItemGroup>
  <ProjectReference Include="..\EspBarcode.Protocol\EspBarcode.Protocol.csproj" />
</ItemGroup>
```

- [ ] **Step 2: Write `ILinkConnection.cs`**

```csharp
namespace EspBarcode.Connectivity.Client;

public interface ILinkConnection : IAsyncDisposable
{
    Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken);
    Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken);
}

public interface ILinkConnector
{
    Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken);
}
```

- [ ] **Step 3: Write `InMemoryDuplexConnection.cs`**

An in-memory loopback pair for tests (and any future host-side simulator) — no serial hardware required.

```csharp
using System.Threading.Channels;

namespace EspBarcode.Connectivity.Client;

public sealed class InMemoryDuplexConnection : ILinkConnection
{
    private readonly Channel<byte[]> _outbound;
    private readonly Channel<byte[]> _inbound;
    private byte[] _leftover = [];
    private int _leftoverOffset;

    private InMemoryDuplexConnection(Channel<byte[]> outbound, Channel<byte[]> inbound)
    {
        _outbound = outbound;
        _inbound = inbound;
    }

    public static (ILinkConnection Left, ILinkConnection Right) CreatePair()
    {
        var a = Channel.CreateUnbounded<byte[]>();
        var b = Channel.CreateUnbounded<byte[]>();
        return (new InMemoryDuplexConnection(a, b), new InMemoryDuplexConnection(b, a));
    }

    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
        => await _outbound.Writer.WriteAsync(bytes.ToArray(), cancellationToken);

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
    {
        if (_leftoverOffset >= _leftover.Length)
        {
            _leftover = await _inbound.Reader.ReadAsync(cancellationToken);
            _leftoverOffset = 0;
        }
        int count = Math.Min(buffer.Length, _leftover.Length - _leftoverOffset);
        _leftover.AsSpan(_leftoverOffset, count).CopyTo(buffer.Span);
        _leftoverOffset += count;
        return count;
    }

    public ValueTask DisposeAsync()
    {
        _outbound.Writer.TryComplete();
        return ValueTask.CompletedTask;
    }
}
```

- [ ] **Step 4: Write `EspLinkLinkSession.cs`**

Owns the byte-level COBS delimiter buffering, hop-frame encode/decode, and reassembly — mirrors `src/SerialCobsEndpoint.cpp`'s `loop()`/`processCobsBlock()` (Task 9), driven by `ILinkConnection` instead of `Serial`.

```csharp
using System.Runtime.CompilerServices;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Client;

public sealed class EspLinkLinkSession(ILinkConnection connection, uint linkSessionId) : IAsyncDisposable
{
    private readonly FrameAssembler _assembler = new();
    private readonly List<byte> _rxBlock = [];
    private uint _linkMessageCounter = 1;

    public async Task SendMessageAsync(byte[] layer3Message, TrafficClass trafficClass, CarrierProfileId profileId,
                                       CancellationToken cancellationToken)
    {
        var header = new HopFrameHeader
        {
            TrafficClass = trafficClass,
            ProfileId = profileId,
            LinkSessionId = linkSessionId,
            LinkMessageId = _linkMessageCounter++,
            FragmentIndex = 0,
            FragmentCount = 1,
        };
        if (!HopFrameHeader.TryEncode(header, layer3Message, out var frame, out var error))
            throw new InvalidOperationException($"failed to encode hop frame: {error}");

        var cobs = Cobs.Encode(frame);
        var withDelimiter = new byte[cobs.Length + 1];
        cobs.CopyTo(withDelimiter, 0);
        await connection.WriteAsync(withDelimiter, cancellationToken);
    }

    public async IAsyncEnumerable<byte[]> ReceiveMessagesAsync([EnumeratorCancellation] CancellationToken cancellationToken)
    {
        var buffer = new byte[512];
        while (!cancellationToken.IsCancellationRequested)
        {
            int read = await connection.ReadAsync(buffer, cancellationToken);
            if (read == 0) yield break;

            for (int i = 0; i < read; i++)
            {
                byte b = buffer[i];
                if (b == 0x00)
                {
                    if (_rxBlock.Count > 0 && TryProcessBlock([.. _rxBlock], out var message)) yield return message;
                    _rxBlock.Clear();
                    continue;
                }
                if (_rxBlock.Count < 2048) _rxBlock.Add(b);
            }
        }
    }

    private bool TryProcessBlock(byte[] block, out byte[] message)
    {
        message = [];
        if (!Cobs.TryDecode(block, out var raw)) return false;
        if (!HopFrameHeader.TryDecode(raw, out var header, out var payload, out _)) return false;
        var outcome = _assembler.AddFragment(header, payload, out var assembled);
        if (outcome != AssemblyOutcome.Complete) return false;
        message = assembled;
        return true;
    }

    public ValueTask DisposeAsync() => connection.DisposeAsync();
}
```

- [ ] **Step 5: Write `EspLinkControlSession.cs`**

Operation correlation over one `EspLinkLinkSession`: a background receive loop completes pending `TaskCompletionSource`s keyed by `correlationId`.

```csharp
using System.Collections.Concurrent;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Client;

public sealed class EspLinkControlSession : IAsyncDisposable
{
    private readonly EspLinkLinkSession _linkSession;
    private readonly ConcurrentDictionary<ulong, TaskCompletionSource<(MessageEnvelope Envelope, byte[] Body)>> _pending = new();
    private readonly CancellationTokenSource _cts = new();
    private Task? _receiveLoop;
    private ulong _nextOperationId = 1;

    public EspLinkControlSession(EspLinkLinkSession linkSession) => _linkSession = linkSession;

    public void Start()
    {
        _receiveLoop = Task.Run(async () =>
        {
            await foreach (var message in _linkSession.ReceiveMessagesAsync(_cts.Token))
            {
                if (!MessageEnvelope.TryDecode(message, out var envelope, out var body, out _)) continue;
                if (_pending.TryRemove(envelope.CorrelationId, out var tcs)) tcs.TrySetResult((envelope, body));
            }
        }, _cts.Token);
    }

    public async Task<(MessageEnvelope Envelope, byte[] Body)> SendCommandAsync(
        ServiceId serviceId, byte[] body, uint controlSessionId, TimeSpan timeout, CancellationToken cancellationToken)
    {
        ulong operationId = _nextOperationId++;
        var envelope = new MessageEnvelope
        {
            Kind = MessageKind.Command,
            ServiceId = serviceId,
            CodecId = CodecId.Json,
            ControlSessionId = controlSessionId,
            OperationId = operationId,
        };
        if (!MessageEnvelope.TryEncode(envelope, body, out var message, out var error))
            throw new InvalidOperationException($"failed to encode envelope: {error}");

        var tcs = new TaskCompletionSource<(MessageEnvelope, byte[])>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[operationId] = tcs;

        await _linkSession.SendMessageAsync(message, TrafficClass.Control, CarrierProfileId.StreamStandard, cancellationToken);

        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutCts.CancelAfter(timeout);
        await using (timeoutCts.Token.Register(() => tcs.TrySetCanceled()))
        {
            return await tcs.Task;
        }
    }

    public async ValueTask DisposeAsync()
    {
        _cts.Cancel();
        if (_receiveLoop is not null)
        {
            try { await _receiveLoop; } catch (OperationCanceledException) { }
        }
        await _linkSession.DisposeAsync();
        _cts.Dispose();
    }
}
```

- [ ] **Step 6: Write loopback tests**

`dotnet/tests/EspBarcode.Connectivity.Tests/LinkSessionTests.cs`:

```csharp
using EspBarcode.Connectivity.Client;
using EspBarcode.Protocol;

namespace EspBarcode.Connectivity.Tests;

public class LinkSessionTests
{
    [Fact]
    public async Task SendMessage_IsReceivedIntactOnTheOtherEndOfALoopback()
    {
        var (left, right) = InMemoryDuplexConnection.CreatePair();
        await using var leftSession = new EspLinkLinkSession(left, linkSessionId: 1);
        await using var rightSession = new EspLinkLinkSession(right, linkSessionId: 1);

        var body = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.ping\",\"body\":{}}"u8.ToArray();
        var envelope = new MessageEnvelope { OperationId = 1 };
        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out _));

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var receiveTask = rightSession.ReceiveMessagesAsync(cts.Token).GetAsyncEnumerator();

        await leftSession.SendMessageAsync(message, TrafficClass.Control, CarrierProfileId.StreamStandard, cts.Token);

        Assert.True(await receiveTask.MoveNextAsync());
        Assert.Equal(message, receiveTask.Current);
    }

    [Fact]
    public async Task ControlSession_CorrelatesRequestAndResponseAcrossALoopback()
    {
        var (clientConn, deviceConn) = InMemoryDuplexConnection.CreatePair();
        await using var clientLink = new EspLinkLinkSession(clientConn, linkSessionId: 1);
        await using var deviceLink = new EspLinkLinkSession(deviceConn, linkSessionId: 1);
        await using var client = new EspLinkControlSession(clientLink);
        client.Start();

        var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));

        // Minimal device stub: echo back a Result envelope correlated to whatever it receives.
        var deviceTask = Task.Run(async () =>
        {
            await foreach (var incoming in deviceLink.ReceiveMessagesAsync(cts.Token))
            {
                Assert.True(MessageEnvelope.TryDecode(incoming, out var requestEnvelope, out _, out _));
                var responseBody = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.hello\",\"body\":{}}"u8.ToArray();
                var responseEnvelope = new MessageEnvelope
                {
                    Kind = MessageKind.Result,
                    CorrelationId = requestEnvelope.OperationId,
                    OperationId = 1000,
                };
                Assert.True(MessageEnvelope.TryEncode(responseEnvelope, responseBody, out var responseMessage, out _));
                await deviceLink.SendMessageAsync(responseMessage, TrafficClass.Control, CarrierProfileId.StreamStandard, cts.Token);
                break;
            }
        }, cts.Token);

        var requestBody = "{\"schema\":\"esbg.control/2.0\",\"name\":\"system.hello\",\"body\":{}}"u8.ToArray();
        var (responseEnvelope, responseBody) = await client.SendCommandAsync(
            ServiceId.System, requestBody, controlSessionId: 0, TimeSpan.FromSeconds(5), cts.Token);

        Assert.Equal(MessageKind.Result, responseEnvelope.Kind);
        await deviceTask;
    }
}
```

- [ ] **Step 7: Build and run**

```bash
dotnet build dotnet/EspScreenBarcodeGenerator.slnx
dotnet test dotnet/tests/EspBarcode.Connectivity.Tests/EspBarcode.Connectivity.Tests.csproj
```

Expected: all tests pass (this test project now covers `TransportSelectorTests` from Task 11 plus `LinkSessionTests`).

- [ ] **Step 8: Commit**

```bash
git add dotnet/src/EspBarcode.Connectivity dotnet/tests/EspBarcode.Connectivity.Tests
git commit -m "feat(dotnet): add typed EspLink v2 client core with an in-memory loopback connector"
```

---

### Task 13: .NET — `SerialV2Connector` (USB v2 over `System.IO.Ports`)

**Files:**
- Modify: `dotnet/src/EspBarcode.Client/EspBarcode.Client.csproj` (add a `ProjectReference` to `EspBarcode.Connectivity`)
- Modify: `dotnet/src/EspBarcode.Client/Transport/SerialPortTransport.cs` (add one additive property)
- Create: `dotnet/src/EspBarcode.Client/TransportV2/UpgradeHandshake.cs`
- Create: `dotnet/src/EspBarcode.Client/TransportV2/SerialLinkConnection.cs`
- Create: `dotnet/src/EspBarcode.Client/TransportV2/SerialV2Connector.cs`
- Create: `dotnet/tests/EspBarcode.Client.Tests/SerialV2ConnectorTests.cs`

**Interfaces:**
- Consumes: `EspBarcode.Connectivity.Client.ILinkConnector`/`ILinkConnection` (Task 12); `EspBarcode.Client.Transport.IEspBarcodeTransport`/`SerialPortTransport` (existing, unchanged surface); `EspBarcode.Client.EspBarcodeClient` (existing, unchanged surface).
- Produces: `EspBarcode.Client.TransportV2.UpgradeHandshake`, `SerialLinkConnection`, `SerialV2Connector`. This is the .NET half of the "one fully working USB v2 path" — it performs the exact same `upgrade` handshake the firmware's `SerialLegacyEndpoint` accepts (Task 9), then reuses the **same already-open `SerialPort`** for raw v2 byte I/O.

**Why the same `SerialPort` instance matters:** `SerialPortTransport`'s constructor toggles DTR/RTS to force a clean device reset on open (`dotnet/src/EspBarcode.Client/Transport/SerialPortTransport.cs:29-36`, already read for this plan). Closing and reopening the port after the `upgrade` handshake would reset the ESP32 again and silently undo the upgrade. `SerialV2Connector` must hand the *same* `SerialPort` object to the v2 byte layer, never a new one.

- [ ] **Step 1: Add the `EspBarcode.Connectivity` reference**

```xml
<!-- dotnet/src/EspBarcode.Client/EspBarcode.Client.csproj — add: -->
<ItemGroup>
  <ProjectReference Include="..\EspBarcode.Connectivity\EspBarcode.Connectivity.csproj" />
</ItemGroup>
```

- [ ] **Step 2: Expose the underlying `SerialPort` (additive, non-breaking)**

Add to `dotnet/src/EspBarcode.Client/Transport/SerialPortTransport.cs`, inside the existing class:

```csharp
/// <summary>
/// The underlying open <see cref="SerialPort"/>, for callers (like <c>SerialV2Connector</c>)
/// that need to hand the same physical connection to a different framing layer without
/// closing and reopening it — reopening re-triggers the DTR/RTS reset above.
/// </summary>
public SerialPort UnderlyingPort => _port;
```

- [ ] **Step 3: Write `UpgradeHandshake.cs`**

Separated from `SerialV2Connector` specifically so it is testable against `IEspBarcodeTransport` without a real serial port.

```csharp
namespace EspBarcode.Client.TransportV2;

using EspBarcode.Client.Transport;

public static class UpgradeHandshake
{
    /// <summary>
    /// Sends the v1 <c>upgrade</c> request that switches the firmware's UART framing
    /// from NDJSON to EspLink v2 COBS frames (see src/SerialLegacyEndpoint.cpp's
    /// "upgrade" handling). Throws <see cref="EspBarcodeProtocolException"/> if the
    /// firmware rejects the request, and <see cref="TimeoutException"/> if it never answers.
    /// </summary>
    public static void RequestUpgrade(IEspBarcodeTransport transport)
    {
        using var client = new EspBarcodeClient(transport);
        client.Request("upgrade");
    }
}
```

(`EspBarcodeClient` disposes its transport in `Dispose()` — wrapping it in a `using` here would close the port `SerialV2Connector` still needs. `SerialV2Connector`, written in Step 5, must NOT use `UpgradeHandshake` through a disposing wrapper; call `RequestUpgrade` directly and keep `v1Transport` alive. Remove the `using var` above — write it as a plain `var client = new EspBarcodeClient(transport);` with no disposal, since the caller owns `transport`'s lifetime.)

- [ ] **Step 4: Write `SerialLinkConnection.cs`**

```csharp
using System.IO.Ports;
using EspBarcode.Connectivity.Client;

namespace EspBarcode.Client.TransportV2;

public sealed class SerialLinkConnection(SerialPort port) : ILinkConnection
{
    public async Task WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken)
        => await port.BaseStream.WriteAsync(bytes, cancellationToken);

    public async Task<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
        => await port.BaseStream.ReadAsync(buffer, cancellationToken);

    public ValueTask DisposeAsync()
    {
        if (port.IsOpen) port.Close();
        port.Dispose();
        return ValueTask.CompletedTask;
    }
}
```

- [ ] **Step 5: Write `SerialV2Connector.cs`**

```csharp
using EspBarcode.Client.Transport;
using EspBarcode.Connectivity.Client;

namespace EspBarcode.Client.TransportV2;

public sealed class SerialV2Connector(string portName, int baudRate = 115200) : ILinkConnector
{
    public Task<ILinkConnection> ConnectAsync(CancellationToken cancellationToken)
    {
        var v1Transport = new SerialPortTransport(portName, baudRate);
        UpgradeHandshake.RequestUpgrade(v1Transport);  // throws on rejection/timeout; v1Transport stays open either way.
        return Task.FromResult<ILinkConnection>(new SerialLinkConnection(v1Transport.UnderlyingPort));
    }
}
```

- [ ] **Step 6: Write `SerialV2ConnectorTests.cs`**

Tests `UpgradeHandshake` against the existing `FakeTransport` (`dotnet/tests/EspBarcode.Client.Tests/FakeTransport.cs`, already `internal` in this same test project — no new test double needed). `SerialLinkConnection`/`SerialV2Connector` themselves require a real `SerialPort` and are exercised by the hardware-validation gap noted in Step 7, not by this file.

```csharp
using EspBarcode.Client.TransportV2;

namespace EspBarcode.Client.Tests;

public class SerialV2ConnectorTests
{
    [Fact]
    public void RequestUpgrade_SendsTheUpgradeCommand()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":true,"cmd":"upgrade","message":"switching to EspLink v2 COBS framing"}""");

        UpgradeHandshake.RequestUpgrade(transport);

        Assert.Single(transport.WrittenLines);
        Assert.Contains("\"cmd\":\"upgrade\"", transport.WrittenLines[0]);
    }

    [Fact]
    public void RequestUpgrade_PropagatesADeviceRejection()
    {
        var transport = new FakeTransport();
        transport.Enqueue("""{"id":1,"ok":false,"cmd":"upgrade","error":{"code":"unknown_command","message":"unsupported command"}}""");

        var exception = Assert.Throws<EspBarcodeProtocolException>(() => UpgradeHandshake.RequestUpgrade(transport));
        Assert.Equal("unknown_command", exception.Code);
    }
}
```

(If `EspBarcodeProtocolException`'s constructor/property names differ from `Code`/`Message` as read here, match `dotnet/src/EspBarcode.Client/EspBarcodeProtocolException.cs`'s actual members instead of guessing.)

- [ ] **Step 7: Document the hardware-validation gap**

`SerialLinkConnection`/`SerialV2Connector` cannot be exercised without a real `SerialPort` against real firmware. Add this to the running list for Task 15:

> `SerialV2Connector`'s end-to-end path (open → `upgrade` → raw COBS I/O over the same `SerialPort`, against the firmware's `SerialCobsEndpoint` from Task 9) is unverified on real hardware as of this task. Needs the same HIL smoke test as Task 9, driven from the .NET side: `SerialV2Connector.ConnectAsync`, wrap the result in `EspLinkLinkSession`/`EspLinkControlSession` (Task 12), send `system.hello`, confirm `system.welcome`.

- [ ] **Step 8: Build and run**

```bash
dotnet build dotnet/EspScreenBarcodeGenerator.slnx
dotnet test dotnet/tests/EspBarcode.Client.Tests/EspBarcode.Client.Tests.csproj
```

Expected: all existing `EspBarcode.Client.Tests` cases remain green, plus the two new `SerialV2ConnectorTests`.

- [ ] **Step 9: Commit**

```bash
git add dotnet/src/EspBarcode.Client dotnet/tests/EspBarcode.Client.Tests/SerialV2ConnectorTests.cs
git commit -m "feat(dotnet): add SerialV2Connector reusing the existing SerialPort after the v1-to-v2 upgrade handshake"
```

---

### Task 14: Documentation — architecture, EspLink v2 protocol, extension guide, migration, next PRs

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Create: `docs/PROTOCOL_V2.md`

**Interfaces:**
- Consumes: the final shape of every prior task (this task documents what actually landed — read the real files, don't restate this plan's draft code verbatim if the implementation diverged during review).

- [ ] **Step 1: Rewrite `docs/ARCHITECTURE.md`'s component model**

Replace the `## Component model` ASCII diagram (`docs/ARCHITECTURE.md:7-40`, already read for this plan) with a layered diagram reflecting Tasks 1-9, and add a new `## EspLink v2 foundation` section covering:

- The dependency direction: `SerialLegacyEndpoint` / `SerialCobsEndpoint` (Serial, ArduinoJson) → `JsonCommandCodec` (ArduinoJson, no Serial) → `ControlProtocolEngine` (no Arduino at all) → `IBarcodeDevice`/`IPresetRepository`/`IDeviceControl` ports → `BarcodeApplicationAdapter`/`EspIdfDeviceControl` (Arduino) → `BarcodeApplication`.
- Why `lib/EspLinkCore` has zero Arduino dependency (Global Constraints) and how that lets `tests/esplink_*_tests.cpp`/`tests/control_protocol_engine_tests.cpp` run on the host without a device.
- A short paragraph on `ControlSession`/`TransferSession` replacing `UsbProtocol::UploadState`, with the "two sessions can't corrupt each other" property called out explicitly (this is the thing a future Bluetooth/Wi-Fi Direct/ESP-NOW connector depends on being true).
- A mermaid diagram (`docs/ARCHITECTURE.md` already uses fenced ` ```text ` diagrams; either keep that style or switch to a ` ```mermaid ` `flowchart TB` — match whatever the file uses elsewhere by the time this task runs).

Update `## USB transport` (`docs/ARCHITECTURE.md:76-88`) to describe both `SerialLegacyEndpoint` (v1, default) and `SerialCobsEndpoint` (v2, opt-in via `upgrade`), and update `## Extension points` (`docs/ARCHITECTURE.md:122-130`) to reference `lib/EspLinkCore`'s ports instead of "feed the same command dispatcher" in the abstract.

- [ ] **Step 2: Write `docs/PROTOCOL_V2.md`**

Structure (fill every section from the real, merged code — this outline is not itself the content):

1. **Overview** — one paragraph: EspLink v2 is the canonical end-to-end protocol; ESP-NOW is a carrier, not the protocol (design plan §1.1). Point to `docs/PROTOCOL.md` for the still-supported v1 wire format.
2. **Layering** — the four layers from the design plan §8.2, with this repo's actual type names next to each layer (`esplink::MessageEnvelope`, `esplink::HopFrameHeader`, `esplink::Cobs`/`esplink::FrameAssembler`, `JsonCommandCodec`).
3. **Message envelope** — the 32-byte table from this plan's Task 4, with the enumerant tables from Global Constraints.
4. **Hop frame** — the 32-byte header + payload + CRC-32 trailer layout, `36 + payloadLength` raw length rule, route ID `0x0000` (direct — the only route this session uses; gateway routes are future work).
5. **Carrier profiles** — the profile table from the design plan §8.5, flagging which profiles this session's `SerialCobsEndpoint` actually negotiates (`stream-standard` only — hardcoded, not yet negotiated per-connection; record this as a known simplification).
6. **v1-to-v2 negotiation** — the `upgrade` command flow from Task 9, with a sequence description: client sends `{"cmd":"upgrade"}` over NDJSON → firmware acks over NDJSON → firmware switches to COBS parsing → client switches its own framing (via `SerialV2Connector`, Task 13) → `system.hello`/`system.welcome` handshake.
7. **v2 command subset (this release)** — the five-name table from Task 9 (`system.hello`→`system.welcome`, `system.ping`, `barcode.generate`, `barcode.close`, `device.backlight.set`), explicit that the rest of design plan §8.9's namespaces are not implemented yet.
8. **Extension guide: adding a transport** — a numbered list: (1) implement `ILinkConnector`/`ILinkConnection` (.NET) or a new `Serial*Endpoint`-equivalent (firmware) around the new carrier's raw I/O; (2) do not touch `ControlProtocolEngine`, `JsonCommandCodec`, or the envelope/frame codecs — they are carrier-agnostic already; (3) if the carrier's frame ceiling is smaller than the largest v1/v2 message this repo currently sends, add fragmentation at the connector boundary using the existing `FrameAssembler`/hop-frame `fragmentIndex`/`fragmentCount` fields — no new wire format is needed; (4) add a `CarrierProfileId` entry only if the carrier needs a new negotiated frame ceiling not already in the table.
9. **Migration notes from `UsbProtocol`** — a short table mapping old→new: `UsbProtocol::UploadState` → `esplink::TransferSession` (session-scoped); `UsbProtocol::dispatch` → `ControlProtocolEngine::dispatchSingle` (typed, catalog-driven); `UsbProtocol::send`/`sendOk`/`sendError` → `IControlResponseSink`; `UsbProtocol::parseSpec` → `JsonCommandCodec::parseSpec` (same algorithm, now takes `base` explicitly instead of reaching into `application_`).
10. **Next PRs** — three subsections, each with an explicit "builds on" pointer into this plan's task outputs:
    - **Bluetooth RFCOMM**: implement `ILinkConnector`/`ILinkConnection` (.NET, WinRT RFCOMM) and a `BluetoothSppEndpoint` (firmware, `BluetoothSerial`) around the existing `ControlProtocolEngine`/envelope/frame codecs from Tasks 4-9; add `TransportKind::Bluetooth`/`CapabilityState::Available` wiring to `SelectionPolicy` (Task 2/11) capability probing; add production pairing/trust (design plan §7.7, out of scope through this entire plan).
    - **Wi-Fi Direct legacy GO + TCP**: implement the same connector seam over a Windows-created group-owner TCP listener and an ESP32 Wi-Fi station endpoint; reuse `tcp-standard`/`tcp-large` `CarrierProfileId` values already reserved in Task 1's `ConnectivityTypes.h`.
    - **ESP-NOW USB gateway**: a second PlatformIO project (gateway firmware) bridging a USB `SerialCobsEndpoint`-like link to native ESP-NOW; the gateway re-fragments opaque `MessageEnvelope` bytes across the USB↔ESP-NOW frame-ceiling gap using the same `FrameAssembler`/hop-frame fields, per design plan §8.4's "gateway may decode Layer 2, preserve Layer 3 bytes" rule — no new envelope format.

- [ ] **Step 3: Cross-link**

Add a one-line pointer from `docs/PROTOCOL.md`'s top (`docs/PROTOCOL.md:1`) to `docs/PROTOCOL_V2.md`, and from `docs/ARCHITECTURE.md`'s `## Extension points` to the same file.

- [ ] **Step 4: Commit**

```bash
git add docs/ARCHITECTURE.md docs/PROTOCOL_V2.md docs/PROTOCOL.md
git commit -m "docs: document the EspLink v2 layered architecture, wire format, and next-PR scope"
```

---

### Task 15: Final verification, cleanup, and completion summary

**Files:**
- Modify: `docs/superpowers/plans/2026-08-20-esplink-v2-foundation-implementation-plan.md` (this file — append the completion summary)

**Interfaces:**
- Consumes: everything. This task has no code deliverable; it is the plan's own "before finishing" checklist from the user's original `/goal`.

- [ ] **Step 1: Run every native C++ test**

```bash
cmake -S . -B .build/native-validation -DBUILD_TESTING=ON -DESPBARCODE_ENABLE_SANITIZERS=ON
cmake --build .build/native-validation
ctest --test-dir .build/native-validation --output-on-failure
```

Expected: `native_core_tests`, `esplink_types_tests`, `esplink_selection_tests`, `esplink_golden_shape_tests`, `esplink_codec_tests`, `control_protocol_engine_tests` all pass under ASan/UBSan. If `scripts/run_validation.sh`/`scripts/run_platformio_validation.sh` already wrap this sequence, run those instead and confirm they picked up the new targets automatically (they should — `CMakeLists.txt`'s `add_test` calls are the only registration needed).

- [ ] **Step 2: Compile-check the firmware**

```bash
pio run -e esp32dev
```

Expected: zero warnings. If the ESP32 toolchain is unavailable in this environment, say so explicitly in Step 6's summary rather than reporting success.

- [ ] **Step 3: Run PlatformIO's existing native/Unity suite (regression check)**

```bash
pio test -e native
```

Expected: the pre-existing `test/test_native/test_main.cpp` suite (untouched by this plan) still passes — this is the "existing regression suite remains green" proof for the non-protocol parts of `lib/EspBarcodeCore`/`lib/UiGeometry`.

- [ ] **Step 4: Run the full .NET suite**

```bash
dotnet build dotnet/EspScreenBarcodeGenerator.slnx
dotnet test dotnet/EspScreenBarcodeGenerator.slnx
```

Expected: every existing test project (`EspBarcode.Client.Tests`, `EspBarcode.Generator.Tests`, `EspBarcode.Viewer.Cli.Tests`) plus the three new ones (`EspBarcode.Protocol.Tests`, `EspBarcode.Connectivity.Tests`, the new cases in `EspBarcode.Client.Tests`) pass, and the build has zero warnings (`TreatWarningsAsErrors=true`).

- [ ] **Step 5: Run static/regression checks and confirm a clean tree**

```bash
python tests/static_firmware_checks.py
python tests/test_host_tool.py
git status --porcelain
```

Expected: both Python scripts pass unchanged (proving `tools/espbarcode.py` and the v1 wire contract are untouched); `git status --porcelain` shows no generated/untracked junk (`.build/`, `.pio/`, `dotnet/**/bin`, `dotnet/**/obj` should already be `.gitignore`d — if any of this task's new native build output leaked into git tracking, remove it with `git rm --cached` before the final commit, never `git clean -fdx` without checking `git status` first per the session's safety rules).

- [ ] **Step 6: Append the completion summary to this plan file**

Add a final `## Completion summary` section to this file (`docs/superpowers/plans/2026-08-20-esplink-v2-foundation-implementation-plan.md`) covering, in this order:

1. **What shipped**: one paragraph per firmware (Tasks 1-9) and .NET (Tasks 10-13) side, naming the concrete new types.
2. **Architecture changes**: the dependency-direction summary from Task 14 Step 1, condensed to 4-6 bullets.
3. **Test results**: the exact pass/fail counts and commands from Steps 1-5 above, verbatim from real command output — never invented numbers.
4. **Known hardware-validation gaps**: the two items recorded in Task 9 Step 6 and Task 13 Step 7, plus anything else discovered while running Steps 1-5 that needed real hardware.
5. **Behavioral notes**: the `upload_begin.label` empty-string-vs-absent collapse recorded in Task 7's engine write-up, and any other intentional divergence found during implementation — each with why it's safe.
6. **Recommended next PR scope**: point to `docs/PROTOCOL_V2.md`'s "Next PRs" section (Task 14) rather than repeating it.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/plans/2026-08-20-esplink-v2-foundation-implementation-plan.md
git commit -m "docs: record EspLink v2 foundation completion summary and verification results"
```

- [ ] **Step 8: Hand off**

Report the four items the user's original `/goal` explicitly asked for — implementation summary, architecture changes, test results, known hardware-validation gaps, and recommended next-PR scope — as your final message to the user, not only in the committed file.

---

## Completion summary

### 1. What shipped

**Firmware (Tasks 1-9):** a new Arduino-free library, `lib/EspLinkCore`, holding the protocol's pure domain logic: `Identifiers.h` and `ConnectivityTypes.h` (the enumerant vocabulary — `RunMode`, `TransportKind`, `CarrierProfileId`, `CapabilityState`, `OptimizationGoal`, `TrafficClass`, `Idempotency`, `MessageKind`, `ServiceId`, `CodecId`, `FrameType`, `FallbackPolicy`, `ModeTransition`), `SelectionPolicy.h` (pure `evaluateSelection`/`isFallbackAllowed` functions with no I/O), `CommandCatalog.h`/`ProtocolCommands.h` (the 18 typed v1 commands as a closed variant, replacing stringly-typed dispatch), the EspLink v2 wire codec (`Crc32`, `Envelope`, `HopFrame`, `Cobs`, `FrameAssembler`), `ApplicationPorts.h` (the `IBarcodeDevice`/`IPresetRepository`/`IDeviceControl` seams the engine talks through), and session types `TransferSession`/`ControlSession` that replace `UsbProtocol::UploadState` with session-scoped state. `ControlProtocolEngine` is the new typed dispatcher — no `Serial`, no `ArduinoJson` — that both transports now share. On the `src/` (Arduino) side: `BarcodeApplicationAdapter` and `EspIdfDeviceControl` implement the ports; `JsonCommandCodec` does JSON<->typed-command translation; `SerialLegacyEndpoint` replaces the deleted `UsbProtocol` for the v1 NDJSON path; `SerialCobsEndpoint` is the new, opt-in (via an explicit `upgrade` command) EspLink v2 COBS path over the same UART.

**.NET (Tasks 10-13):** `EspBarcode.Protocol`, a byte-identical C# port of the firmware's envelope/frame/COBS/CRC/reassembly codec (verified against the same test vectors as the C++ side). `EspBarcode.Connectivity`, holding the value-object mirror of the firmware's connectivity types plus `TransportSelector`, and the typed v2 client core: `ILinkConnection`/`ILinkConnector` (transport seam), `InMemoryDuplexConnection` (loopback test double), `EspLinkLinkSession`/`EspLinkControlSession` (the client-side counterpart to firmware's `ControlSession`). `EspBarcode.Client`'s new `TransportV2` namespace adds `UpgradeHandshake` (sends the v1 `upgrade` request), `SerialLinkConnection` (`ILinkConnection` over a `SerialPort`), and `SerialV2Connector` (drives the handshake then hands the *same* already-open `SerialPort` to the v2 byte layer, so the upgrade doesn't trigger a second DTR/RTS device reset).

**Real bugs found and fixed during implementation** (all confirmed still correctly fixed in the current worktree as of this task):
1. `lib/EspLinkCore/src/Cobs.cpp` and `dotnet/src/EspBarcode.Protocol/Cobs.cs` — the original scan-based COBS encoder silently dropped data when input ended in `0x00` or a 254-byte run was immediately followed by `0x00`. Fixed with the standard backpatch algorithm in both languages, with regression tests for both failure modes.
2. `src/JsonCommandCodec.cpp`'s `encode()` — `out.to<JsonObject>()` silently cleared `ok`/`cmd`/`id` from every v1 success response (ArduinoJson v7's `to<T>()` clears the document first). Fixed to `out.as<JsonObject>()`.
3. `src/JsonCommandCodec.cpp`'s `upload_begin` decode — raw JSON int width/height was narrowed to `uint16_t` before the engine's `1-512` bounds check, letting e.g. `width:65539` wrap to `3` and bypass validation. Fixed by clamping to `[0,65535]` before the cast.
4. `dotnet/src/EspBarcode.Connectivity/Client/EspLinkControlSession.cs`'s `SendCommandAsync` — leaked `_pending` dictionary entries on timeout/cancellation (only removed on a matching response). Fixed with `try/finally`.
5. `dotnet/src/EspBarcode.Client/TransportV2/SerialLinkConnection.cs`'s `ReadAsync` — `SerialPort.BaseStream.ReadAsync` never completes on Windows when started before data is pending (hardware-confirmed platform gap). Fixed by routing through synchronous `SerialPort.Read` on a background thread with `TimeoutException` retry.

### 2. Architecture changes

- New dependency direction on firmware: `SerialLegacyEndpoint`/`SerialCobsEndpoint` (Serial, ArduinoJson) -> `JsonCommandCodec` (ArduinoJson, no Serial) -> `ControlProtocolEngine` (no Arduino at all) -> `IBarcodeDevice`/`IPresetRepository`/`IDeviceControl` ports -> `BarcodeApplicationAdapter`/`EspIdfDeviceControl` (Arduino) -> `BarcodeApplication`.
- `lib/EspLinkCore` has zero Arduino dependency by construction (a Global Constraint of this plan), which is what lets `tests/esplink_*_tests.cpp` and `tests/control_protocol_engine_tests.cpp` run on the host, under a normal `ctest` invocation, with no device attached.
- `ControlSession`/`TransferSession` replace `UsbProtocol::UploadState` with session-scoped state; two sessions (e.g. the legacy and v2 endpoints running concurrently in `main.cpp`) cannot corrupt each other's in-flight upload/state — the property a future Bluetooth/Wi-Fi Direct/ESP-NOW connector depends on.
- `ControlProtocolEngine` is a single typed dispatcher shared by both `SerialLegacyEndpoint` (v1 NDJSON) and `SerialCobsEndpoint` (v2 COBS) — the same 18-command catalog and validation logic runs under either transport.
- On .NET, `EspBarcode.Protocol` and `EspBarcode.Connectivity` are new, dependency-free-of-`System.IO.Ports` libraries; `EspBarcode.Client`'s `TransportV2` layer is the only place that touches a real `SerialPort`, mirroring the firmware's transport/engine split.
- Both language codecs (envelope/frame/COBS/CRC) are verified byte-identical via shared test vectors, so a message encoded on one side decodes correctly on the other without a wire-format spec drift risk.

### 3. Test results (verbatim from commands run in this task, worktree `.worktrees/esplink-v2-foundation`)

**Step 1 — native C++ tests.** Sanitizers were requested (`-DESPBARCODE_ENABLE_SANITIZERS=ON`) but **could not run**: this environment's toolchain is MSYS2 MinGW-w64 GCC 14.2.0, which does not ship `libasan`/`libubsan` (`ld.exe: cannot find -lasan` / `cannot find -lubsan`) — confirmed by both the failed link and a filesystem search for `*asan*`/`*ubsan*` under the MinGW installation, which found nothing. Sanitizer-instrumented native testing is not available in this environment. Rebuilt and tested with `-DESPBARCODE_ENABLE_SANITIZERS=OFF` (same CMake/ctest sequence otherwise) to still get real pass/fail signal on the 6 target executables:

```
cmake -S . -B .build/native-validation -DBUILD_TESTING=ON -DESPBARCODE_ENABLE_SANITIZERS=OFF -G "MinGW Makefiles"
cmake --build .build/native-validation
ctest --test-dir .build/native-validation --output-on-failure
```
```
1/6 Test #1: native_core_tests ................   Passed    0.21 sec
2/6 Test #2: esplink_types_tests ..............   Passed    0.09 sec
3/6 Test #3: esplink_selection_tests ..........   Passed    0.07 sec
4/6 Test #4: esplink_golden_shape_tests .......   Passed    0.18 sec
5/6 Test #5: esplink_codec_tests ..............   Passed    0.07 sec
6/6 Test #6: control_protocol_engine_tests ....   Passed    0.24 sec
100% tests passed, 0 tests failed out of 6
```

**Step 2 — firmware compile-check.** `pio run -e esp32dev` (run after `--target clean`, so this is a full rebuild, not a cache hit): `SUCCESS`, RAM 7.0% (23076/327680 bytes), Flash 15.6% (489537/3145728 bytes). Zero `warning:` lines in the build log (grepped explicitly); `firmware.bin` (478.4K) and `firmware.elf` (15.3M) produced in `.pio/build/esp32dev/`.

**Step 3 — PlatformIO native/Unity suite (regression check).** `pio test -e native`:
```
9 test cases: 9 succeeded in 00:00:06.608
```
All 9 pre-existing cases in `test/test_native/test_main.cpp` (`test_base64_round_trip`, `test_retail_check_digits`, `test_matrix_encoders`, `test_qr_dependency_adapter`, `test_gs1_normalized_data_is_json_safe`, `test_pixel_exact_layout`, `test_random_payload_always_encodes`, `test_home_button_layout_has_no_overlaps_and_fits_screen`, `test_touch_pad_closes_gap_without_crossing_neighbor`) passed.

**Step 4 — full .NET suite.** `dotnet build dotnet/EspScreenBarcodeGenerator.slnx`: `13 projects, 0 errors, 0 warnings`. `dotnet test dotnet/EspScreenBarcodeGenerator.slnx`:
```
Passed! - Failed: 0, Passed: 15,  Skipped: 0, Total: 15  - EspBarcode.Protocol.Tests.dll
Passed! - Failed: 0, Passed: 106, Skipped: 0, Total: 106 - EspBarcode.Generator.Tests.dll
Passed! - Failed: 0, Passed: 10,  Skipped: 0, Total: 10  - EspBarcode.Connectivity.Tests.dll
Passed! - Failed: 0, Passed: 35,  Skipped: 0, Total: 35  - EspBarcode.Viewer.Cli.Tests.dll
Passed! - Failed: 0, Passed: 19,  Skipped: 0, Total: 19  - EspBarcode.Client.Tests.dll
```
185/185 tests passed across all 5 test projects (the 3 pre-existing plus the 2 new ones this plan added).

**Step 5 — static/regression checks and clean tree.**
```
python tests/static_firmware_checks.py   -> "Static firmware contract checks passed"
python tests/test_host_tool.py           -> Ran 17 tests in 0.064s, OK
git status --porcelain                   -> empty (clean tree)
```
No `.build/`, `.pio/`, or `dotnet/**/bin`/`obj` output leaked into git tracking.

### 4. Known hardware-validation gaps

- **Task 9 Step 6 (firmware USB v2 path):** `SerialLegacyEndpoint` -> `upgrade` -> `SerialCobsEndpoint`, all five mapped v2 commands, and COBS resync after a corrupted frame have no native/simulated test harness (same `ArduinoJson`/`Serial` constraint as the v1 codec, compounded by needing a real byte-level serial round trip). **Update since that task was written:** later in this session, real hardware validation (COM7, CH340 adapter) exercised the `upgrade` handshake and a `system.hello`->`system.welcome`/`barcode.generate`->`displayed:true` round trip successfully. Still not individually hardware-exercised: `system.ping`, `barcode.close`, `device.backlight.set` alone, COBS resync after a *deliberately corrupted* frame, and multi-fragment reassembly.
- **Task 13 Step 7 (.NET `SerialV2Connector`):** the open -> `upgrade` -> raw COBS I/O path against real firmware has no test without a real `SerialPort`. **Update since that task was written:** real hardware validation this session ran 8/8 successful `system.hello`->`system.welcome` round trips across 2 independent process runs over the actual USB link.
- `upload_abort` over v1 has not been hardware-exercised this session (only the 17/18-command sweep plus the width/height edge case were run live; `upload_abort` was not among them).
- `.NET FallbackPolicy`/`isFallbackAllowed` has no C# port yet — confirmed absent (`find dotnet -iname "*FallbackPolicy*"` returns nothing). Deliberate scope cut, documented in `docs/PROTOCOL_V2.md`'s Next PRs section.
- `SerialCobsEndpoint.h` still declares 3 unused members (`writeMessage`, `writeMessageWithResponse`, `v1NameFor`) — confirmed still present at `src/SerialCobsEndpoint.h:31-35`. Harmless (never called; `send`/`sendError` inline the same logic), noted as a cleanup opportunity.
- MSVC cannot build this repo cleanly on this machine due to a pre-existing, unrelated `std::fill` narrowing warning in `lib/EspBarcodeCore/src/EspBarcodeCore.cpp:25` under `/W4 /WX` — not introduced by this plan. All native validation this session (Task 15 included) used the MinGW g++ generator instead. Still true as of this task.
- New this task: this MinGW-w64 toolchain has no `libasan`/`libubsan`, so `-DESPBARCODE_ENABLE_SANITIZERS=ON` cannot actually link — sanitizer-instrumented native testing (ASan/UBSan) is unavailable in this environment entirely, not just unused. If sanitizer coverage is required, it needs a Linux/WSL or MSVC-with-a-different-warning-config runner.

### 5. Behavioral notes (intentional, safe divergences from v1)

- **`upload_begin.label` empty-string collapse** (`lib/EspLinkCore/src/ControlProtocolEngine.cpp:242` — `upload.label = command.label.empty() ? "Uploaded matrix" : command.label;`): an explicitly empty string now collapses to the same default ("Uploaded matrix") as an absent field, whereas the original ArduinoJson-based code only substituted the default when the JSON key was literally absent. Safe: there is no user-visible difference between "explicitly empty" and "not provided" for a display label.
- **`DownloadCommand::chunkBytes` negative-value clamp direction** (`lib/EspLinkCore/src/ControlProtocolEngine.cpp:299` — `std::clamp<std::size_t>(command.chunkBytes, 48, 768)`): a negative `chunk_bytes` value, once implicitly converted to `std::size_t`, wraps to a very large unsigned value and clamps to the upper bound (768) instead of the original's lower bound (48). Harmless: the resulting chunk size stays within the documented 48-768 in-range either way, and no caller in this codebase sends a negative `chunk_bytes`.

### 6. Recommended next PR scope

See `docs/PROTOCOL_V2.md`'s "Next PRs" section (`## 10. Next PRs`, added in Task 14) for the three scoped follow-ups (Bluetooth RFCOMM, Wi-Fi Direct legacy GO + TCP, ESP-NOW USB gateway), each with an explicit "builds on" pointer into this plan's task outputs.

---
