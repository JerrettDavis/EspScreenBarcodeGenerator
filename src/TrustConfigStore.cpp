#include "TrustConfigStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "EspBarcodeCore.h"  // bytesToBase64/bytesFromBase64

using namespace esplink;
using namespace espbarcode;

namespace {
constexpr const char* kTrustRecordsPath = "/config/trust.json";
constexpr const char* kIdentityNamespace = "trust-id";

std::string macToString(const std::array<uint8_t, 6>& mac) {
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                 mac[5]);
    return std::string(buffer);
}

bool macFromString(const std::string& text, std::array<uint8_t, 6>& out) {
    unsigned values[6];
    if (std::sscanf(text.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &values[0], &values[1], &values[2], &values[3],
                    &values[4], &values[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) out[static_cast<std::size_t>(i)] = static_cast<uint8_t>(values[i]);
    return true;
}
}  // namespace

bool TrustConfigStore::begin(ITrustCrypto& crypto, std::string& error) {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        error = "LittleFS initialization failed";
        return false;
    }
    if (!LittleFS.exists("/config") && !LittleFS.mkdir("/config")) {
        error = "could not create config directory";
        return false;
    }
    if (!loadIdentity(crypto, error)) return false;
    loadRecords();  // missing/corrupt: starts empty, matching DeviceConfigStore's contract

    Preferences prefs;
    if (prefs.begin(kIdentityNamespace, true)) {
        securePairingEnabled_ = prefs.getBool("secure_on", false);
        prefs.end();
    }
    return true;
}

bool TrustConfigStore::loadIdentity(ITrustCrypto& crypto, std::string& error) {
    Preferences prefs;
    if (!prefs.begin(kIdentityNamespace, false)) {
        error = "could not open trust identity namespace";
        return false;
    }

    std::vector<uint8_t> priv(kTrustPrivateKeyBytes), pub(kTrustPublicKeyBytes);
    const std::size_t readPriv = prefs.getBytes("priv", priv.data(), priv.size());
    const std::size_t readPub = prefs.getBytes("pub", pub.data(), pub.size());
    if (readPriv == kTrustPrivateKeyBytes && readPub == kTrustPublicKeyBytes) {
        std::memcpy(identity_.privateKey.data(), priv.data(), kTrustPrivateKeyBytes);
        std::memcpy(identity_.publicKey.data(), pub.data(), kTrustPublicKeyBytes);
        prefs.end();
        return true;
    }

    // First boot (or a corrupt/partial write): generate a fresh identity and persist it.
    if (!crypto.generateKeyPair(identity_)) {
        prefs.end();
        error = "identity keygen failed";
        return false;
    }
    const bool wrote = prefs.putBytes("priv", identity_.privateKey.data(), kTrustPrivateKeyBytes) ==
                          kTrustPrivateKeyBytes &&
                      prefs.putBytes("pub", identity_.publicKey.data(), kTrustPublicKeyBytes) == kTrustPublicKeyBytes;
    prefs.end();
    if (!wrote) {
        error = "could not persist new identity";
        return false;
    }
    return true;
}

bool TrustConfigStore::saveIdentity(std::string& error) const {
    Preferences prefs;
    if (!prefs.begin(kIdentityNamespace, false)) {
        error = "could not open trust identity namespace";
        return false;
    }
    const bool wrote = prefs.putBytes("priv", identity_.privateKey.data(), kTrustPrivateKeyBytes) ==
                          kTrustPrivateKeyBytes &&
                      prefs.putBytes("pub", identity_.publicKey.data(), kTrustPublicKeyBytes) == kTrustPublicKeyBytes;
    prefs.end();
    if (!wrote) {
        error = "could not persist identity";
        return false;
    }
    return true;
}

bool TrustConfigStore::loadRecords() {
    File file = LittleFS.open(kTrustRecordsPath, "r");
    if (!file) return false;

    JsonDocument document;
    const DeserializationError parseError = deserializeJson(document, file);
    file.close();
    if (parseError) return false;

    store_.clear();
    for (JsonObjectConst entry : document["records"].as<JsonArrayConst>()) {
        TrustRecord record;
        std::vector<uint8_t> keyBytes;
        if (!bytesFromBase64(entry["staticPubKey"] | "", keyBytes) || keyBytes.size() != kTrustPublicKeyBytes) {
            continue;
        }
        std::memcpy(record.staticPublicKey.data(), keyBytes.data(), kTrustPublicKeyBytes);
        if (!macFromString(entry["mac"] | "", record.mac)) continue;
        record.peerRole = (entry["role"] | "client") == std::string("gateway") ? TrustRole::Gateway : TrustRole::Client;
        record.routeId = entry["routeId"] | 0;
        record.pairedAtMs = entry["pairedAtMs"] | 0;
        record.label = std::string(entry["label"] | "");
        store_.add(record);
    }
    return true;
}

bool TrustConfigStore::saveRecords(std::string& error) const {
    JsonDocument document;
    document["schema"] = 1;
    JsonArray records = document["records"].to<JsonArray>();
    for (std::size_t i = 0; i < store_.size(); ++i) {
        const TrustRecord* record = store_.at(i);
        JsonObject entry = records.add<JsonObject>();
        entry["staticPubKey"] =
            bytesToBase64(std::vector<uint8_t>(record->staticPublicKey.begin(), record->staticPublicKey.end()));
        entry["mac"] = macToString(record->mac);
        entry["role"] = record->peerRole == TrustRole::Gateway ? "gateway" : "client";
        entry["routeId"] = record->routeId;
        entry["pairedAtMs"] = record->pairedAtMs;
        entry["label"] = record->label;
    }

    File file = LittleFS.open(kTrustRecordsPath, "w");
    if (!file) {
        error = "could not open trust records for writing";
        return false;
    }
    const std::size_t written = serializeJson(document, file);
    file.close();
    if (written == 0) {
        error = "could not serialize trust records";
        return false;
    }
    return true;
}

bool TrustConfigStore::setSecurePairingEnabled(bool value, std::string& error) {
    Preferences prefs;
    if (!prefs.begin(kIdentityNamespace, false)) {
        error = "could not open trust identity namespace";
        return false;
    }
    const bool wrote = prefs.putBool("secure_on", value);
    prefs.end();
    if (!wrote) {
        error = "could not persist secure pairing toggle";
        return false;
    }
    securePairingEnabled_ = value;
    return true;
}

bool TrustConfigStore::addRecord(const TrustRecord& record, std::string& error) {
    if (!store_.add(record)) {
        error = store_.full() ? "trust list full" : "already paired with this device";
        return false;
    }
    if (!saveRecords(error)) {
        store_.forget(record.staticPublicKey);  // keep in-memory/on-disk consistent
        return false;
    }
    return true;
}

bool TrustConfigStore::forgetRecord(const TrustPublicKey& key, std::string& error) {
    const TrustRecord* existing = store_.findByStaticKey(key);
    if (existing == nullptr) return false;
    const TrustRecord removedCopy = *existing;
    store_.forget(key);
    if (!saveRecords(error)) {
        store_.add(removedCopy);
        return false;
    }
    return true;
}
