#pragma once

#include <cstddef>

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
    {"orientation",    TrafficClass::Control,  Idempotency::Idempotent,   false},
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
