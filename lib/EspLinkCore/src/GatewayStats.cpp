#include "GatewayStats.h"

#include <cstring>

namespace esplink {

void GatewayStats::recordPeerSeen(const uint8_t mac[6], uint32_t nowMs) {
    for (std::size_t i = 0; i < peerCount_; ++i) {
        if (std::memcmp(peers_[i].mac.data(), mac, peers_[i].mac.size()) == 0) {
            peers_[i].lastSeenMs = nowMs;
            return;
        }
    }

    if (peerCount_ < kMaxPeers) {
        std::memcpy(peers_[peerCount_].mac.data(), mac, peers_[peerCount_].mac.size());
        peers_[peerCount_].lastSeenMs = nowMs;
        ++peerCount_;
        return;
    }

    std::size_t oldestIndex = 0;
    for (std::size_t i = 1; i < peerCount_; ++i) {
        if (peers_[i].lastSeenMs < peers_[oldestIndex].lastSeenMs) oldestIndex = i;
    }
    std::memcpy(peers_[oldestIndex].mac.data(), mac, peers_[oldestIndex].mac.size());
    peers_[oldestIndex].lastSeenMs = nowMs;
}

void GatewayStats::recordHostActivity(uint32_t nowMs) {
    hostEverSeen_ = true;
    hostLastSeenMs_ = nowMs;
}

GatewayStats::Snapshot GatewayStats::snapshot(uint32_t nowMs, uint32_t hostTimeoutMs) const {
    Snapshot snap;
    snap.peers = peers_;
    snap.peerCount = peerCount_;
    snap.hostEverSeen = hostEverSeen_;
    snap.hostLastSeenMs = hostLastSeenMs_;
    snap.nowMs = nowMs;
    snap.hostConnected = hostEverSeen_ && (nowMs - hostLastSeenMs_) <= hostTimeoutMs;
    return snap;
}

}  // namespace esplink
