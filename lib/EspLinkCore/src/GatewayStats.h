#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

    // deviceId is bounded (not std::string) to keep this hot-path bookkeeping allocation-free,
    // matching the class's existing "portable, allocation-free" contract.
    static constexpr std::size_t kMaxDeviceIdLen = 23;

    struct Peer {
        std::array<uint8_t, 6> mac{};
        uint32_t lastSeenMs = 0;
        bool everRelayed = false;  // has carried at least one real (non-discovery) relayed message
        bool everPinged = false;   // has answered at least one gateway.link.pong discovery probe
        uint32_t lastRttMs = 0;    // valid only if everPinged
        std::array<char, kMaxDeviceIdLen + 1> deviceId{};  // NUL-terminated; empty if unknown

        const char* deviceIdCStr() const { return deviceId.data(); }
    };

    struct Snapshot {
        std::array<Peer, kMaxPeers> peers{};
        std::size_t peerCount = 0;
        bool hostEverSeen = false;
        bool hostConnected = false;
        uint32_t hostLastSeenMs = 0;
        uint32_t nowMs = 0;
    };

    // Records (or refreshes) a peer sighting from real relayed traffic. Once kMaxPeers distinct
    // MACs have been observed, the least-recently-seen peer is evicted for the new one, so a
    // flaky/rotating sender can't wedge out a peer that's still actively relaying.
    void recordPeerSeen(const uint8_t mac[6], uint32_t nowMs);

    // Records a reply to this gateway's own discovery probe (gateway.link.pong), separate from
    // recordPeerSeen since a prospective client may answer pings long before (or without ever)
    // carrying real relayed traffic. deviceId is truncated to kMaxDeviceIdLen if longer.
    void recordDiscoveryPong(const uint8_t mac[6], uint32_t nowMs, uint32_t rttMs, const char* deviceId);

    // Marks the USB host as active as of nowMs. Call once per received host byte/frame.
    void recordHostActivity(uint32_t nowMs);

    // hostConnected is true only once recordHostActivity has been called at least once and
    // nowMs - <last recorded activity> <= hostTimeoutMs.
    Snapshot snapshot(uint32_t nowMs, uint32_t hostTimeoutMs = 3000) const;

private:
    // Finds the peer slot for `mac`, inserting (or evicting the least-recently-seen slot) if
    // this MAC hasn't been seen before. Shared by recordPeerSeen/recordDiscoveryPong so both
    // "channels" of sighting a peer (relay traffic vs. discovery pong) update the same entry.
    std::size_t findOrCreatePeerIndex(const uint8_t mac[6], uint32_t nowMs);

    std::array<Peer, kMaxPeers> peers_{};
    std::size_t peerCount_ = 0;
    bool hostEverSeen_ = false;
    uint32_t hostLastSeenMs_ = 0;
};

}  // namespace esplink
