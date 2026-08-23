#include "TrustStore.h"

#include <cstdio>

namespace esplink {

std::string trustFingerprint(const TrustHash& hash) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "%02X%02X-%02X%02X", hash[0], hash[1], hash[2], hash[3]);
    return std::string(buffer);
}

const TrustRecord* TrustStore::findByMac(const std::array<uint8_t, 6>& mac) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (records_[i].mac == mac) return &records_[i];
    }
    return nullptr;
}

const TrustRecord* TrustStore::findByStaticKey(const TrustPublicKey& key) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (records_[i].staticPublicKey == key) return &records_[i];
    }
    return nullptr;
}

const TrustRecord* TrustStore::at(std::size_t index) const { return index < count_ ? &records_[index] : nullptr; }

bool TrustStore::add(TrustRecord record) {
    if (full()) return false;
    if (findByStaticKey(record.staticPublicKey) != nullptr) return false;

    bool routeIdUsed[kMaxRecords + 1] = {};
    for (std::size_t i = 0; i < count_; ++i) {
        if (records_[i].routeId >= 1 && records_[i].routeId <= kMaxRecords) routeIdUsed[records_[i].routeId] = true;
    }

    // Honor a routeId the record already carries -- a record reloaded from persistence, or one
    // re-added by a rollback after a failed write. Reassigning on every load would silently
    // permute which peer a given routeId points at (forget() is a swap-remove, so the persisted
    // order changes after any revocation), turning a stale cached routeId on the host side from
    // "drops" into "delivers to the wrong client".
    //
    // Falls back to the lowest free slot when the record has no routeId yet (0 -- the default for
    // a freshly-paired device), or when the stored id is out of range or already taken by another
    // record (a corrupt or hand-edited trust.json): duplicate route ids would make a routeId
    // lookup ambiguous, so a conflicting stored id is deliberately not honored.
    const bool canKeepStoredRouteId =
        record.routeId >= 1 && record.routeId <= kMaxRecords && !routeIdUsed[record.routeId];
    if (!canKeepStoredRouteId) {
        uint16_t assigned = 0;
        for (uint16_t candidate = 1; candidate <= kMaxRecords; ++candidate) {
            if (!routeIdUsed[candidate]) {
                assigned = candidate;
                break;
            }
        }
        record.routeId = assigned;
    }

    records_[count_++] = record;
    return true;
}

bool TrustStore::forget(const TrustPublicKey& key) {
    for (std::size_t i = 0; i < count_; ++i) {
        if (records_[i].staticPublicKey == key) {
            records_[i] = records_[count_ - 1];
            --count_;
            return true;
        }
    }
    return false;
}

}  // namespace esplink
