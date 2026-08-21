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
