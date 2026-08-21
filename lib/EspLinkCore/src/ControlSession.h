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
