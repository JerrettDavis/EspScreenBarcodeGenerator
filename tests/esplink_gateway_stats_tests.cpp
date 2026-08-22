#include "GatewayStats.h"

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

std::array<uint8_t, 6> macFor(uint8_t last) {
    return {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, last};
}

void test_no_peers_or_host_initially() {
    GatewayStats stats;
    const GatewayStats::Snapshot snap = stats.snapshot(1000);
    CHECK(snap.peerCount == 0);
    CHECK(!snap.hostEverSeen);
    CHECK(!snap.hostConnected);
}

void test_records_new_peer() {
    GatewayStats stats;
    const auto mac = macFor(1);
    stats.recordPeerSeen(mac.data(), 100);

    const GatewayStats::Snapshot snap = stats.snapshot(100);
    CHECK(snap.peerCount == 1);
    CHECK(snap.peers[0].mac == mac);
    CHECK(snap.peers[0].lastSeenMs == 100);
}

void test_updates_existing_peer_last_seen_without_growing_count() {
    GatewayStats stats;
    const auto mac = macFor(1);
    stats.recordPeerSeen(mac.data(), 100);
    stats.recordPeerSeen(mac.data(), 500);

    const GatewayStats::Snapshot snap = stats.snapshot(500);
    CHECK(snap.peerCount == 1);
    CHECK(snap.peers[0].lastSeenMs == 500);
}

void test_tracks_multiple_distinct_peers() {
    GatewayStats stats;
    stats.recordPeerSeen(macFor(1).data(), 100);
    stats.recordPeerSeen(macFor(2).data(), 200);
    stats.recordPeerSeen(macFor(3).data(), 300);

    const GatewayStats::Snapshot snap = stats.snapshot(300);
    CHECK(snap.peerCount == 3);
}

void test_evicts_least_recently_seen_peer_once_full() {
    GatewayStats stats;
    for (std::size_t i = 0; i < GatewayStats::kMaxPeers; ++i) {
        stats.recordPeerSeen(macFor(static_cast<uint8_t>(i)).data(), static_cast<uint32_t>(100 * (i + 1)));
    }
    // Peer 0 has the smallest lastSeenMs (100) and should be the eviction target.
    const auto newMac = macFor(200);
    stats.recordPeerSeen(newMac.data(), 999999);

    const GatewayStats::Snapshot snap = stats.snapshot(999999);
    CHECK(snap.peerCount == GatewayStats::kMaxPeers);

    bool foundNew = false;
    bool foundEvicted = false;
    const auto evictedMac = macFor(0);
    for (std::size_t i = 0; i < snap.peerCount; ++i) {
        if (snap.peers[i].mac == newMac) foundNew = true;
        if (snap.peers[i].mac == evictedMac) foundEvicted = true;
    }
    CHECK(foundNew);
    CHECK(!foundEvicted);
}

void test_host_connected_within_timeout() {
    GatewayStats stats;
    stats.recordHostActivity(1000);

    const GatewayStats::Snapshot snap = stats.snapshot(3000, /*hostTimeoutMs=*/3000);
    CHECK(snap.hostEverSeen);
    CHECK(snap.hostConnected);
    CHECK(snap.hostLastSeenMs == 1000);
}

void test_host_disconnected_after_timeout() {
    GatewayStats stats;
    stats.recordHostActivity(1000);

    const GatewayStats::Snapshot snap = stats.snapshot(10000, /*hostTimeoutMs=*/3000);
    CHECK(snap.hostEverSeen);
    CHECK(!snap.hostConnected);
}

void test_host_activity_refreshes_last_seen() {
    GatewayStats stats;
    stats.recordHostActivity(1000);
    stats.recordHostActivity(5000);

    const GatewayStats::Snapshot snap = stats.snapshot(5000, /*hostTimeoutMs=*/3000);
    CHECK(snap.hostConnected);
    CHECK(snap.hostLastSeenMs == 5000);
}

}  // namespace

int main() {
    test_no_peers_or_host_initially();
    test_records_new_peer();
    test_updates_existing_peer_last_seen_without_growing_count();
    test_tracks_multiple_distinct_peers();
    test_evicts_least_recently_seen_peer_once_full();
    test_host_connected_within_timeout();
    test_host_disconnected_after_timeout();
    test_host_activity_refreshes_last_seen();
    if (failures != 0) {
        std::cerr << failures << " esplink gateway stats test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All esplink gateway stats tests passed\n";
    return EXIT_SUCCESS;
}
