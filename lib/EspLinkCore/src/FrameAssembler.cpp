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
