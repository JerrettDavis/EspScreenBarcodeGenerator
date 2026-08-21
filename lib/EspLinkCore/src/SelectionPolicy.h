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
