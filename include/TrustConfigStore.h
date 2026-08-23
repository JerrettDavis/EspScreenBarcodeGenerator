#pragma once

#include <string>

#include "TrustCryptoPort.h"
#include "TrustStore.h"

// Persists this device's trust state: its own persistent identity keypair (NVS, via
// Preferences -- private key material never goes in the LittleFS JSON file) and its trust
// records (LittleFS JSON, mirroring DeviceConfigStore's pattern). ESP32-only, like
// DeviceConfigStore -- the portable esplink::TrustStore it wraps knows nothing about either
// storage mechanism.
class TrustConfigStore {
public:
    // Loads persisted state, generating a fresh identity keypair on first boot if none exists
    // yet. `crypto` must already be begin()'d (Task 4). Returns false only on an unrecoverable
    // storage failure (LittleFS/Preferences init failure, or identity keygen failure) -- a
    // missing or corrupt trust-records file just starts with zero records, same as
    // DeviceConfigStore::load()'s "missing/corrupt keeps defaults" contract.
    bool begin(esplink::ITrustCrypto& crypto, std::string& error);

    const esplink::TrustKeyPair& identity() const { return identity_; }
    const esplink::TrustStore& store() const { return store_; }
    esplink::TrustStore& mutableStore() { return store_; }

    bool securePairingEnabled() const { return securePairingEnabled_; }
    bool setSecurePairingEnabled(bool value, std::string& error);

    // Adds a record and persists it. Fails (nothing changed) if the store is full or the record
    // already exists (see TrustStore::add), or if the write fails.
    bool addRecord(const esplink::TrustRecord& record, std::string& error);

    // Removes a record by static key and persists the change. Returns true only if a record was
    // actually removed and the write succeeded (removed-but-write-failed re-adds the record and
    // returns false, keeping in-memory and on-disk state consistent).
    bool forgetRecord(const esplink::TrustPublicKey& key, std::string& error);

    // Public write-through for callers that mutate mutableStore() directly (e.g. a pairing
    // handshake completing and adding/updating a record via the TrustStore API rather than
    // addRecord/forgetRecord). Thin wrapper over the same on-disk write addRecord/forgetRecord
    // already use internally.
    bool persistRecords(std::string& error) const { return saveRecords(error); }

private:
    bool loadIdentity(esplink::ITrustCrypto& crypto, std::string& error);
    bool saveIdentity(std::string& error) const;
    bool loadRecords();
    bool saveRecords(std::string& error) const;

    esplink::TrustKeyPair identity_{};
    esplink::TrustStore store_;
    bool securePairingEnabled_ = false;
};
