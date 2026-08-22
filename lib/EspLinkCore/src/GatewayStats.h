#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esplink {

// Portable, allocation-free bookkeeping for GatewayRelay's live monitoring stats
// (docs/PROTOCOL_V2.md §10). Gateway mode has no pairing handshake -- any ESP-NOW sender is
// accepted -- so "negotiated clients" here means distinct peer MACs seen so far this session,
// and "heartbeat" is the recency of the last frame from each peer / the USB host rather than a
// distinct keepalive message. GatewayRelay feeds this from its RX/TX paths; the UI reads
// snapshot() each redraw.
class GatewayStats {
public:
    static constexpr std::size_t kMaxPeers = 8;

    struct Peer {
        std::array<uint8_t, 6> mac{};
        uint32_t lastSeenMs = 0;
    };

    struct Snapshot {
        std::array<Peer, kMaxPeers> peers{};
        std::size_t peerCount = 0;
        bool hostEverSeen = false;
        bool hostConnected = false;
        uint32_t hostLastSeenMs = 0;
        uint32_t nowMs = 0;
    };

    // Records (or refreshes) a peer sighting. Once kMaxPeers distinct MACs have been observed,
    // the least-recently-seen peer is evicted for the new one, so a flaky/rotating sender can't
    // wedge out a peer that's still actively relaying.
    void recordPeerSeen(const uint8_t mac[6], uint32_t nowMs);

    // Marks the USB host as active as of nowMs. Call once per received host byte/frame.
    void recordHostActivity(uint32_t nowMs);

    // hostConnected is true only once recordHostActivity has been called at least once and
    // nowMs - <last recorded activity> <= hostTimeoutMs.
    Snapshot snapshot(uint32_t nowMs, uint32_t hostTimeoutMs = 3000) const;

private:
    std::array<Peer, kMaxPeers> peers_{};
    std::size_t peerCount_ = 0;
    bool hostEverSeen_ = false;
    uint32_t hostLastSeenMs_ = 0;
};

}  // namespace esplink
