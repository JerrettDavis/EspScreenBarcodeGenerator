#pragma once

#include "TrustCryptoPort.h"

#include <array>
#include <cstdint>
#include <string>

namespace esplink {

enum class TrustRole : uint8_t { Gateway, Client };

struct TrustRecord {
    TrustPublicKey staticPublicKey{};
    std::array<uint8_t, 6> mac{};
    TrustRole peerRole = TrustRole::Client;
    uint16_t routeId = 0;
    uint32_t pairedAtMs = 0;
    std::string label;
    // Anti-replay watermark (spec §2): the highest HopFrameHeader.linkMessageId seen from this
    // peer since it was loaded/added this boot. Deliberately runtime-only -- never persisted to
    // trust.json, never round-tripped through add()'s "preserve on reload" path below. Both sides'
    // real linkMessageId counters reset to 1 on every reboot (see EspNowEndpoint/GatewayRelay's
    // linkMessageCounter_), so persisting a prior session's high-water mark would reject the
    // peer's very first (legitimately renumbered) frame after either device restarts.
    uint32_t lastSeenLinkMessageId = 0;
};

// Formats the first 4 bytes of a SHA-256 hash (see ITrustCrypto::sha256 over a static public
// key) as "XXXX-XXXX" upper-hex groups, e.g. {0xA3,0xF9,0x21,0xC4,...} -> "A3F9-21C4".
std::string trustFingerprint(const TrustHash& hash);

// In-memory trust record management, independent of persistence format -- an ESP32-only adapter
// (include/TrustConfigStore.h, Task 6) loads/saves these to LittleFS+ArduinoJson; this class
// knows nothing about files or JSON, matching how every other lib/EspLinkCore type stays
// portable and natively testable.
class TrustStore {
public:
    // Matches ESP-NOW's hardware-encrypted-peer ceiling (ESP_NOW_MAX_ENCRYPT_PEER_NUM) --
    // confirmed against esp_now.h for this IDF release in Task 8; adjust here if it differs.
    static constexpr std::size_t kMaxRecords = 6;

    std::size_t size() const { return count_; }
    bool full() const { return count_ >= kMaxRecords; }

    const TrustRecord* findByMac(const std::array<uint8_t, 6>& mac) const;
    const TrustRecord* findByStaticKey(const TrustPublicKey& key) const;
    const TrustRecord* at(std::size_t index) const;

    // Adds a new record. If `record.routeId` is already set to an in-range id that no other
    // record holds (i.e. the record came back from persistence, or from an add/forget rollback),
    // that id is preserved so route ids stay stable across reboots and revocations. Otherwise --
    // routeId 0, out of range, or colliding -- the lowest free id in [1, kMaxRecords] is assigned.
    // Fails (store unchanged) if already full() or a record with this staticPublicKey already
    // exists -- call forget() first to replace one.
    bool add(TrustRecord record);

    // Removes the record matching this static key, if any. Returns true if something was removed.
    bool forget(const TrustPublicKey& key);

    // Anti-replay check (spec §2 "Anti-replay"): true and advances the watermark if `linkMessageId`
    // is strictly greater than the last one seen from `mac` (accepted, not a replay); false and
    // unchanged otherwise -- either `mac` isn't a trust record at all, or `linkMessageId` is a
    // duplicate/reordered/replayed frame that must be dropped rather than processed again.
    bool checkAndAdvanceReplayGuard(const std::array<uint8_t, 6>& mac, uint32_t linkMessageId);

    void clear() { count_ = 0; }

private:
    std::array<TrustRecord, kMaxRecords> records_{};
    std::size_t count_ = 0;
};

}  // namespace esplink
