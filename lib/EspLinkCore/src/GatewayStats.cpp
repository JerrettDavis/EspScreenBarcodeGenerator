#include "GatewayStats.h"

#include <algorithm>
#include <cstring>

namespace esplink {

std::size_t GatewayStats::findOrCreatePeerIndex(const uint8_t mac[6], uint32_t nowMs) {
    for (std::size_t i = 0; i < peerCount_; ++i) {
        if (std::memcmp(peers_[i].mac.data(), mac, peers_[i].mac.size()) == 0) return i;
    }

    if (peerCount_ < kMaxPeers) {
        const std::size_t index = peerCount_++;
        peers_[index] = Peer{};
        std::memcpy(peers_[index].mac.data(), mac, peers_[index].mac.size());
        peers_[index].lastSeenMs = nowMs;
        return index;
    }

    std::size_t oldestIndex = 0;
    for (std::size_t i = 1; i < peerCount_; ++i) {
        if (peers_[i].lastSeenMs < peers_[oldestIndex].lastSeenMs) oldestIndex = i;
    }
    peers_[oldestIndex] = Peer{};
    std::memcpy(peers_[oldestIndex].mac.data(), mac, peers_[oldestIndex].mac.size());
    peers_[oldestIndex].lastSeenMs = nowMs;
    return oldestIndex;
}

void GatewayStats::recordPeerSeen(const uint8_t mac[6], uint32_t nowMs) {
    const std::size_t index = findOrCreatePeerIndex(mac, nowMs);
    peers_[index].lastSeenMs = nowMs;
    peers_[index].everRelayed = true;
}

void GatewayStats::recordDiscoveryPong(const uint8_t mac[6], uint32_t nowMs, uint32_t rttMs, const char* deviceId) {
    const std::size_t index = findOrCreatePeerIndex(mac, nowMs);
    Peer& peer = peers_[index];
    peer.lastSeenMs = nowMs;
    peer.everPinged = true;
    peer.lastRttMs = rttMs;
    const std::size_t len = std::min(std::strlen(deviceId), kMaxDeviceIdLen);
    std::memcpy(peer.deviceId.data(), deviceId, len);
    peer.deviceId[len] = '\0';
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
