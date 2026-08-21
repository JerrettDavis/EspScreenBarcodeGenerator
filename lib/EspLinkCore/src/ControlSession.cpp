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
