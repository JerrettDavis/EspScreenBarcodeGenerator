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

    // Adds a new record, assigning the lowest routeId in [1, kMaxRecords] not already in use.
    // Fails (store unchanged) if already full() or a record with this staticPublicKey already
    // exists -- call forget() first to replace one.
    bool add(TrustRecord record);

    // Removes the record matching this static key, if any. Returns true if something was removed.
    bool forget(const TrustPublicKey& key);

    void clear() { count_ = 0; }

private:
    std::array<TrustRecord, kMaxRecords> records_{};
    std::size_t count_ = 0;
};

}  // namespace esplink
