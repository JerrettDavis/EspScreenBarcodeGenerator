# ESP-NOW Secure Pairing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ECDH/ECDSA (P-256) pairing with on-screen numeric-comparison approval to ESP-NOW, so that with "Secure Pairing" enabled, no unpaired peer can talk to a gateway or client, and reconnecting an already-trusted peer is silent (trust-on-first-use).

**Architecture:** A portable, natively-testable core (`lib/EspLinkCore`: crypto port interface, trust record store, handshake helpers, pairing state machine) is driven by ESP32-only glue (`src/TrustCrypto.cpp` wrapping mbedTLS, `include/TrustConfigStore.h`+`src/TrustConfigStore.cpp` wrapping LittleFS+Preferences) and wired into the two existing ESP-NOW entry points (`EspNowEndpoint`, `GatewayRelay`), a new on-device Trust screen, and `.NET`/Blazor management UI over the existing USB gateway channel.

**Tech Stack:** C++17 (Arduino/ESP-IDF via PlatformIO `espressif32@7.0.1`), mbedTLS (bundled with the ESP32 Arduino core — no new on-device library dependency), ArduinoJson 7.4.3, Unity (native tests), .NET 10 / Blazor WebAssembly, Reqnroll + Playwright (E2E).

**Spec:** `docs/superpowers/specs/2026-08-22-espnow-secure-pairing-design.md`

## Global Constraints

- Curve: NIST P-256 (secp256r1) for both ECDSA (identity signing) and ECDH (per-handshake ephemeral key agreement) — one curve family, no new on-device crypto library.
- Key derivation: HKDF-SHA256 (RFC 5869) over the ECDH shared secret, deriving a 16-byte ESP-NOW LMK + a 6-digit numeric comparison code + a 32-byte transcript hash (signed for mutual confirm) in one pass.
- `ServiceId::Trust = 6` is already reserved in `lib/EspLinkCore/src/ConnectivityTypes.h` — do not renumber. New JSON command names: `trust.pair.begin`, `trust.pair.confirm`, `trust.pair.cancel`, `trust.controllers.list`, `trust.controller.forget`.
- `HopFrameHeader.routeId` (already a field, currently always 0) is used by `GatewayRelay` to target a specific paired client once Secure Pairing is on.
- Default state: Secure Pairing is **off** on every device until a human turns it on in Settings. Turning it on immediately drops unpaired-peer traffic (no grace period).
- Trust record cap: `TrustStore::kMaxRecords = 6`, matching ESP-NOW's hardware-encrypted-peer ceiling (`ESP_NOW_MAX_ENCRYPT_PEER_NUM`) — Task 8 confirms the exact constant for this IDF release and adjusts if it differs.
- `lib/EspLinkCore` code must stay portable (no Arduino/ESP-IDF/mbedTLS headers) so it keeps compiling and testing in `env:native`, matching every existing file in that directory.
- Pairing handshake messages (`trust.pair.begin/confirm/cancel`) are unicast (temporary unencrypted `esp_now_add_peer`), not broadcast — only the existing `gateway.link.ping`/`pong` discovery stays broadcast.

---

## Task 1: Trust crypto port & handshake message types

**Files:**
- Create: `lib/EspLinkCore/src/TrustCryptoPort.h`
- Create: `lib/EspLinkCore/src/TrustHandshake.h`
- Create: `lib/EspLinkCore/src/TrustHandshake.cpp`
- Test: `test/test_native/test_main.cpp` (add test functions + register them)

**Interfaces:**
- Produces: `esplink::TrustPublicKey/TrustPrivateKey/TrustSignature/TrustNonce/TrustHash` (fixed-size `std::array` aliases), `TrustKeyPair`, `TrustDerivedKeys`, `ITrustCrypto` (abstract crypto port), `TrustHelloMessage`, `buildTrustHello`/`verifyTrustHello`/`deriveFromHellos` free functions. Every later task consumes these exact names/signatures.

- [ ] **Step 1: Write `TrustCryptoPort.h`**

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esplink {

// P-256 (secp256r1) sizes: uncompressed SEC1 point (0x04||X||Y), a 32-byte scalar, and a
// fixed-size r||s ECDSA signature (not DER, so it has a constant wire size).
inline constexpr std::size_t kTrustPublicKeyBytes = 65;
inline constexpr std::size_t kTrustPrivateKeyBytes = 32;
inline constexpr std::size_t kTrustSignatureBytes = 64;
inline constexpr std::size_t kTrustNonceBytes = 16;
inline constexpr std::size_t kTrustLmkBytes = 16;
inline constexpr std::size_t kTrustHashBytes = 32;

using TrustPublicKey = std::array<uint8_t, kTrustPublicKeyBytes>;
using TrustPrivateKey = std::array<uint8_t, kTrustPrivateKeyBytes>;
using TrustSignature = std::array<uint8_t, kTrustSignatureBytes>;
using TrustNonce = std::array<uint8_t, kTrustNonceBytes>;
using TrustHash = std::array<uint8_t, kTrustHashBytes>;

struct TrustKeyPair {
    TrustPrivateKey privateKey{};
    TrustPublicKey publicKey{};
};

// HKDF-SHA256 output of one pairing handshake, split three ways (see deriveSessionKeys).
struct TrustDerivedKeys {
    std::array<uint8_t, kTrustLmkBytes> lmk{};  // installed as this peer's esp_now_peer_info_t.lmk
    uint32_t numericCode = 0;                   // 0..999999, shown on both screens for comparison
    TrustHash transcriptHash{};                 // what the mutual "Confirm" step signs/verifies
};

// Abstract crypto port. lib/EspLinkCore stays free of mbedTLS/Arduino headers (so it keeps
// compiling and unit-testing in env:native) by depending only on this interface; the ESP32
// build supplies a real implementation (src/TrustCrypto.cpp, Task 5) and native tests supply a
// deterministic fake (test/test_native/FakeTrustCrypto.h, Task 3).
// Every method is `const`: implementations (TrustCrypto, Task 4; FakeTrustCrypto, Step 4 below)
// keep their RNG/entropy state at file scope, not as object fields that need mutation, so calling
// this port from an otherwise-const method (e.g. EspNowEndpoint::pairingStatus() const, Task 6)
// doesn't require a mutable/const_cast workaround.
class ITrustCrypto {
public:
    virtual ~ITrustCrypto() = default;

    virtual bool generateKeyPair(TrustKeyPair& out) const = 0;
    virtual bool generateNonce(TrustNonce& out) const = 0;

    // Signs/verifies an arbitrary-length message (the implementation hashes it internally, so
    // callers never pre-hash before calling sign/verify).
    virtual bool sign(const TrustPrivateKey& privateKey, const uint8_t* message, std::size_t messageLength,
                      TrustSignature& outSignature) const = 0;
    virtual bool verify(const TrustPublicKey& publicKey, const uint8_t* message, std::size_t messageLength,
                        const TrustSignature& signature) const = 0;

    virtual bool sha256(const uint8_t* data, std::size_t length, TrustHash& out) const = 0;

    // Computes the ECDH shared secret from (ourEphemeralPrivateKey, peerEphemeralPublicKey), then
    // HKDF-SHA256-derives lmk/numericCode/transcriptHash from
    // (sharedSecret, ourNonce, peerNonce, ourStaticPublicKey, peerStaticPublicKey). Implementations
    // MUST canonicalize the two (nonce, staticKey) pairs identically regardless of call order
    // (e.g. by comparing the two static public keys byte-for-byte and always hashing the
    // lexicographically smaller one first) so both sides of a handshake derive identical output.
    virtual bool deriveSessionKeys(const TrustPrivateKey& ourEphemeralPrivateKey,
                                   const TrustPublicKey& ourEphemeralPublicKey,
                                   const TrustPublicKey& ourStaticPublicKey, const TrustNonce& ourNonce,
                                   const TrustPublicKey& peerEphemeralPublicKey,
                                   const TrustPublicKey& peerStaticPublicKey, const TrustNonce& peerNonce,
                                   TrustDerivedKeys& out) const = 0;
};

}  // namespace esplink
```

- [ ] **Step 2: Write `TrustHandshake.h`**

```cpp
#pragma once

#include "TrustCryptoPort.h"

namespace esplink {

// The wire shape of `trust.pair.begin` (and a responder's reply to it) — see
// docs/superpowers/specs/2026-08-22-espnow-secure-pairing-design.md §2.
struct TrustHelloMessage {
    TrustPublicKey staticPublicKey{};
    TrustPublicKey ephemeralPublicKey{};
    TrustNonce nonce{};
    TrustSignature signature{};  // ECDSA(ourIdentity, ephemeralPublicKey || nonce)
};

// Generates a fresh ephemeral keypair + nonce and signs them with `ourIdentity`, filling
// `outHello` to send. `outEphemeral`/`outNonce` must be kept by the caller for the later
// deriveFromHellos() call. Returns false (nothing generated) only on a crypto failure.
bool buildTrustHello(ITrustCrypto& crypto, const TrustKeyPair& ourIdentity, TrustKeyPair& outEphemeral,
                     TrustNonce& outNonce, TrustHelloMessage& outHello);

// Verifies a peer's hello signature against `expectedStaticKey`. For first-time pairing, pass
// `peerHello.staticPublicKey` itself (this only proves self-consistency — the sender holds the
// private key matching what it claims, not that the claim should be trusted). For a reconnect to
// an already-trusted peer, pass the *stored* trusted key instead, so a MAC-spoofing attacker
// presenting a different static key is rejected outright rather than "verified against itself".
bool verifyTrustHello(ITrustCrypto& crypto, const TrustPublicKey& expectedStaticKey,
                      const TrustHelloMessage& peerHello);

// Derives session keys once both hellos are known: ourEphemeral/ourNonce are this side's own
// values from buildTrustHello(); peerHello is the already-verified incoming message.
bool deriveFromHellos(ITrustCrypto& crypto, const TrustKeyPair& ourIdentity, const TrustKeyPair& ourEphemeral,
                     const TrustNonce& ourNonce, const TrustHelloMessage& peerHello, TrustDerivedKeys& outDerived);

}  // namespace esplink
```

- [ ] **Step 3: Write `TrustHandshake.cpp`**

```cpp
#include "TrustHandshake.h"

#include <cstring>

namespace esplink {

namespace {
// Layout signed by a hello: ephemeralPublicKey || nonce.
std::array<uint8_t, kTrustPublicKeyBytes + kTrustNonceBytes> helloSignedBytes(const TrustPublicKey& ephemeralPublicKey,
                                                                              const TrustNonce& nonce) {
    std::array<uint8_t, kTrustPublicKeyBytes + kTrustNonceBytes> out{};
    std::memcpy(out.data(), ephemeralPublicKey.data(), kTrustPublicKeyBytes);
    std::memcpy(out.data() + kTrustPublicKeyBytes, nonce.data(), kTrustNonceBytes);
    return out;
}
}  // namespace

bool buildTrustHello(ITrustCrypto& crypto, const TrustKeyPair& ourIdentity, TrustKeyPair& outEphemeral,
                     TrustNonce& outNonce, TrustHelloMessage& outHello) {
    if (!crypto.generateKeyPair(outEphemeral)) return false;
    if (!crypto.generateNonce(outNonce)) return false;

    const auto signed_ = helloSignedBytes(outEphemeral.publicKey, outNonce);
    TrustSignature signature{};
    if (!crypto.sign(ourIdentity.privateKey, signed_.data(), signed_.size(), signature)) return false;

    outHello.staticPublicKey = ourIdentity.publicKey;
    outHello.ephemeralPublicKey = outEphemeral.publicKey;
    outHello.nonce = outNonce;
    outHello.signature = signature;
    return true;
}

bool verifyTrustHello(ITrustCrypto& crypto, const TrustPublicKey& expectedStaticKey,
                      const TrustHelloMessage& peerHello) {
    if (peerHello.staticPublicKey != expectedStaticKey) return false;
    const auto signed_ = helloSignedBytes(peerHello.ephemeralPublicKey, peerHello.nonce);
    return crypto.verify(expectedStaticKey, signed_.data(), signed_.size(), peerHello.signature);
}

bool deriveFromHellos(ITrustCrypto& crypto, const TrustKeyPair& ourIdentity, const TrustKeyPair& ourEphemeral,
                     const TrustNonce& ourNonce, const TrustHelloMessage& peerHello, TrustDerivedKeys& outDerived) {
    return crypto.deriveSessionKeys(ourEphemeral.privateKey, ourEphemeral.publicKey, ourIdentity.publicKey, ourNonce,
                                    peerHello.ephemeralPublicKey, peerHello.staticPublicKey, peerHello.nonce,
                                    outDerived);
}

}  // namespace esplink
```

- [ ] **Step 4: Add `test/test_native/FakeTrustCrypto.h`** — a deterministic, non-cryptographic `ITrustCrypto` test double shared by this task and Task 4. It treats "public key" and "private key" as the *same* bytes (there is no real asymmetry), so `verify()` can recompute what `sign()` would have produced and compare — this drives handshake *logic* tests without a real crypto library in the native build.

```cpp
#pragma once

#include "TrustCryptoPort.h"

#include <cstring>

namespace esplink {

class FakeTrustCrypto : public ITrustCrypto {
public:
    bool generateKeyPair(TrustKeyPair& out) const override {
        const uint8_t seed = nextSeed_++;
        out.privateKey.fill(seed);
        out.publicKey.fill(seed);  // fake: "public" == "private", see class comment
        return true;
    }

    bool generateNonce(TrustNonce& out) const override {
        out.fill(nextSeed_++);
        return true;
    }

    bool sign(const TrustPrivateKey& privateKey, const uint8_t* message, std::size_t messageLength,
              TrustSignature& outSignature) const override {
        outSignature = fakeMac(privateKey.data(), privateKey.size(), message, messageLength);
        return true;
    }

    bool verify(const TrustPublicKey& publicKey, const uint8_t* message, std::size_t messageLength,
                const TrustSignature& signature) const override {
        return fakeMac(publicKey.data(), publicKey.size(), message, messageLength) == signature;
    }

    bool sha256(const uint8_t* data, std::size_t length, TrustHash& out) const override {
        out.fill(0);
        for (std::size_t i = 0; i < length; ++i) out[i % out.size()] ^= data[i];
        return true;
    }

    bool deriveSessionKeys(const TrustPrivateKey& ourEphemeralPrivateKey, const TrustPublicKey&,
                           const TrustPublicKey& ourStaticPublicKey, const TrustNonce& ourNonce,
                           const TrustPublicKey& peerEphemeralPublicKey, const TrustPublicKey& peerStaticPublicKey,
                           const TrustNonce& peerNonce, TrustDerivedKeys& out) const override {
        // "Shared secret": fake ECDH is just XOR-ing both ephemeral keys (ourEphemeralPrivateKey
        // is numerically equal to our ephemeral public key in this fake, see generateKeyPair).
        std::array<uint8_t, kTrustPublicKeyBytes> shared{};
        for (std::size_t i = 0; i < shared.size(); ++i) {
            shared[i] = ourEphemeralPrivateKey[i % ourEphemeralPrivateKey.size()] ^
                        peerEphemeralPublicKey[i % peerEphemeralPublicKey.size()];
        }

        // Canonicalize so both sides hash identically regardless of role (real implementation
        // must do the same — see the interface comment on deriveSessionKeys).
        const bool ourStaticIsSmaller = std::memcmp(ourStaticPublicKey.data(), peerStaticPublicKey.data(),
                                                     kTrustPublicKeyBytes) < 0;
        const TrustPublicKey& firstStatic = ourStaticIsSmaller ? ourStaticPublicKey : peerStaticPublicKey;
        const TrustPublicKey& secondStatic = ourStaticIsSmaller ? peerStaticPublicKey : ourStaticPublicKey;
        const TrustNonce& firstNonce = ourStaticIsSmaller ? ourNonce : peerNonce;
        const TrustNonce& secondNonce = ourStaticIsSmaller ? peerNonce : ourNonce;

        std::array<uint8_t, kTrustPublicKeyBytes * 2 + kTrustNonceBytes * 2> transcript{};
        std::size_t offset = 0;
        std::memcpy(transcript.data() + offset, shared.data(), shared.size());
        offset += shared.size();
        std::memcpy(transcript.data() + offset, firstStatic.data(), firstStatic.size());
        offset += firstStatic.size();
        std::memcpy(transcript.data() + offset, secondStatic.data(), secondStatic.size());
        offset += secondStatic.size();
        std::memcpy(transcript.data() + offset, firstNonce.data(), firstNonce.size());
        offset += firstNonce.size();
        std::memcpy(transcript.data() + offset, secondNonce.data(), secondNonce.size());

        TrustHash hash{};
        sha256(transcript.data(), transcript.size(), hash);
        out.transcriptHash = hash;
        std::memcpy(out.lmk.data(), hash.data(), out.lmk.size());
        out.numericCode = (static_cast<uint32_t>(hash[0]) << 24 | static_cast<uint32_t>(hash[1]) << 16 |
                           static_cast<uint32_t>(hash[2]) << 8 | hash[3]) %
                          1000000u;
        return true;
    }

private:
    TrustSignature fakeMac(const uint8_t* key, std::size_t keyLength, const uint8_t* message,
                           std::size_t messageLength) const {
        TrustSignature out{};
        for (std::size_t i = 0; i < messageLength; ++i) out[i % out.size()] ^= message[i];
        for (std::size_t i = 0; i < keyLength; ++i) out[i % out.size()] ^= key[i];
        return out;
    }

    // Mutated from const methods above (generateKeyPair/generateNonce) -- this fake's whole point
    // is producing distinct deterministic values per call without any real entropy source, and
    // ITrustCrypto's contract keeps state at file scope precisely so implementations don't need a
    // mutable-state escape hatch for real RNG. This one small counter is the exception, local to
    // the test double, not the interface.
    mutable uint8_t nextSeed_ = 1;
};

}  // namespace esplink
```

- [ ] **Step 5: Add handshake tests to `test/test_native/test_main.cpp`**

```cpp
#include "FakeTrustCrypto.h"
#include "TrustHandshake.h"

void test_trust_hello_round_trip() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair aliceIdentity, bobIdentity;
    TEST_ASSERT_TRUE(crypto.generateKeyPair(aliceIdentity));
    TEST_ASSERT_TRUE(crypto.generateKeyPair(bobIdentity));

    esplink::TrustKeyPair aliceEphemeral, bobEphemeral;
    esplink::TrustNonce aliceNonce{}, bobNonce{};
    esplink::TrustHelloMessage aliceHello, bobHello;
    TEST_ASSERT_TRUE(esplink::buildTrustHello(crypto, aliceIdentity, aliceEphemeral, aliceNonce, aliceHello));
    TEST_ASSERT_TRUE(esplink::buildTrustHello(crypto, bobIdentity, bobEphemeral, bobNonce, bobHello));

    // Self-consistency (first-time pairing): each side accepts the other's own claimed key.
    TEST_ASSERT_TRUE(esplink::verifyTrustHello(crypto, aliceHello.staticPublicKey, aliceHello));
    TEST_ASSERT_TRUE(esplink::verifyTrustHello(crypto, bobHello.staticPublicKey, bobHello));

    // A tampered signature must not verify.
    esplink::TrustHelloMessage tampered = aliceHello;
    tampered.signature[0] ^= 0xFF;
    TEST_ASSERT_FALSE(esplink::verifyTrustHello(crypto, tampered.staticPublicKey, tampered));

    // Claiming a static key that doesn't match the embedded one is rejected outright.
    esplink::TrustHelloMessage spoofed = aliceHello;
    TEST_ASSERT_FALSE(esplink::verifyTrustHello(crypto, bobHello.staticPublicKey, spoofed));

    esplink::TrustDerivedKeys aliceDerived, bobDerived;
    TEST_ASSERT_TRUE(
        esplink::deriveFromHellos(crypto, aliceIdentity, aliceEphemeral, aliceNonce, bobHello, aliceDerived));
    TEST_ASSERT_TRUE(
        esplink::deriveFromHellos(crypto, bobIdentity, bobEphemeral, bobNonce, aliceHello, bobDerived));

    // Both sides must land on identical derived output despite opposite call order.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(aliceDerived.lmk.data(), bobDerived.lmk.data(), aliceDerived.lmk.size());
    TEST_ASSERT_EQUAL_UINT32(aliceDerived.numericCode, bobDerived.numericCode);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(aliceDerived.transcriptHash.data(), bobDerived.transcriptHash.data(),
                                 aliceDerived.transcriptHash.size());
}
```

Register it in `test_main.cpp`'s `main()`/`setup()` `RUN_TEST(...)` list next to the existing tests.

- [ ] **Step 6: Run the native test suite and verify it passes**

Run: `pio test -e native`
Expected: all tests pass, including `test_trust_hello_round_trip`.

- [ ] **Step 7: Commit**

```bash
git add lib/EspLinkCore/src/TrustCryptoPort.h lib/EspLinkCore/src/TrustHandshake.h \
        lib/EspLinkCore/src/TrustHandshake.cpp test/test_native/FakeTrustCrypto.h \
        test/test_native/test_main.cpp
git commit -m "feat(trust): add crypto port interface and handshake hello/derive helpers"
```

---

## Task 2: Trust record store

**Files:**
- Create: `lib/EspLinkCore/src/TrustStore.h`
- Create: `lib/EspLinkCore/src/TrustStore.cpp`
- Test: `test/test_native/test_main.cpp`

**Interfaces:**
- Consumes: `TrustPublicKey`, `TrustHash` (Task 1).
- Produces: `TrustRole`, `TrustRecord`, `trustFingerprint()`, `TrustStore` (with `size()/full()/findByMac()/findByStaticKey()/at()/add()/forget()/clear()`). Tasks 6–8 persist/consume `TrustRecord` and enforce traffic against `TrustStore`.

- [ ] **Step 1: Write `TrustStore.h`**

```cpp
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
```

- [ ] **Step 2: Write `TrustStore.cpp`**

```cpp
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
    uint16_t assigned = 0;
    for (uint16_t candidate = 1; candidate <= kMaxRecords; ++candidate) {
        if (!routeIdUsed[candidate]) {
            assigned = candidate;
            break;
        }
    }
    record.routeId = assigned;

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
```

- [ ] **Step 3: Add tests to `test/test_native/test_main.cpp`**

```cpp
#include "TrustStore.h"

void test_trust_store_add_find_forget() {
    esplink::TrustStore store;
    esplink::TrustRecord record;
    record.staticPublicKey.fill(0x11);
    record.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    record.peerRole = esplink::TrustRole::Client;

    TEST_ASSERT_TRUE(store.add(record));
    TEST_ASSERT_EQUAL_UINT32(1, store.size());

    const esplink::TrustRecord* byMac = store.findByMac(record.mac);
    TEST_ASSERT_NOT_NULL(byMac);
    TEST_ASSERT_EQUAL_UINT16(1, byMac->routeId);  // first assigned routeId is 1

    const esplink::TrustRecord* byKey = store.findByStaticKey(record.staticPublicKey);
    TEST_ASSERT_NOT_NULL(byKey);

    // Duplicate static key is rejected.
    esplink::TrustRecord duplicate = record;
    duplicate.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    TEST_ASSERT_FALSE(store.add(duplicate));
    TEST_ASSERT_EQUAL_UINT32(1, store.size());

    TEST_ASSERT_TRUE(store.forget(record.staticPublicKey));
    TEST_ASSERT_EQUAL_UINT32(0, store.size());
    TEST_ASSERT_NULL(store.findByMac(record.mac));
}

void test_trust_store_enforces_cap() {
    esplink::TrustStore store;
    for (std::size_t i = 0; i < esplink::TrustStore::kMaxRecords; ++i) {
        esplink::TrustRecord record;
        record.staticPublicKey.fill(static_cast<uint8_t>(i + 1));
        record.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, static_cast<uint8_t>(i)};
        TEST_ASSERT_TRUE(store.add(record));
    }
    TEST_ASSERT_TRUE(store.full());

    esplink::TrustRecord overflow;
    overflow.staticPublicKey.fill(0xFF);
    overflow.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_FALSE(store.add(overflow));
}

void test_trust_fingerprint_format() {
    esplink::TrustHash hash{};
    hash[0] = 0xA3;
    hash[1] = 0xF9;
    hash[2] = 0x21;
    hash[3] = 0xC4;
    TEST_ASSERT_EQUAL_STRING("A3F9-21C4", esplink::trustFingerprint(hash).c_str());
}
```

Register the three new tests via `RUN_TEST(...)`.

- [ ] **Step 4: Run the native test suite and verify it passes**

Run: `pio test -e native`
Expected: all tests pass, including the three new `test_trust_store_*` / `test_trust_fingerprint_format` tests.

- [ ] **Step 5: Commit**

```bash
git add lib/EspLinkCore/src/TrustStore.h lib/EspLinkCore/src/TrustStore.cpp test/test_native/test_main.cpp
git commit -m "feat(trust): add in-memory trust record store"
```

---

## Task 3: Pairing state machine

**Files:**
- Create: `lib/EspLinkCore/src/TrustPairingSession.h`
- Create: `lib/EspLinkCore/src/TrustPairingSession.cpp`
- Test: `test/test_native/test_main.cpp`

**Interfaces:**
- Consumes: everything from Task 1 (`ITrustCrypto`, `TrustHelloMessage`, `buildTrustHello`/`verifyTrustHello`/`deriveFromHellos`, `TrustKeyPair`, `TrustDerivedKeys`, `TrustSignature`).
- Produces: `TrustPairingState`, `TrustPairingOutcome`, `TrustPairingSession` (`beginAsInitiator`, `onPeerHello`, `currentOutcome`, `confirmLocally`, `onPeerConfirm`, `cancel`, `tick`, `derivedKeys`, `peerHello`, `state`, `reset`). Tasks 7–8 wire this into `EspNowEndpoint`/`GatewayRelay`.

- [ ] **Step 1: Write `TrustPairingSession.h`**

```cpp
#pragma once

#include "TrustCryptoPort.h"
#include "TrustHandshake.h"

#include <cstdint>

namespace esplink {

enum class TrustPairingState : uint8_t {
    Idle,                // nothing in progress
    AwaitingPeerHello,   // we sent our hello (initiator), waiting for the peer's reply
    AwaitingApproval,    // both hellos exchanged; showing fingerprint+code, waiting for a local tap
    AwaitingPeerConfirm, // we confirmed first; waiting for the peer's confirm to commit
    Committed,           // trust established this attempt -- caller persists it, then reset()
    Cancelled,           // denied, timed out, or a signature failed to verify
};

struct TrustPairingOutcome {
    TrustPublicKey peerStaticPublicKey{};
    uint32_t numericCode = 0;
};

// Drives one pairing attempt end-to-end. Not thread-safe; call only from the main loop task.
// One instance handles one attempt at a time -- EspNowEndpoint/GatewayRelay each own a single
// instance and call reset() between attempts (Committed/Cancelled do not auto-reset, so the
// caller can still read derivedKeys()/peerHello() once before starting the next attempt).
class TrustPairingSession {
public:
    explicit TrustPairingSession(ITrustCrypto& crypto) : crypto_(crypto) {}

    // Starts this device as the initiator. Fails (state unchanged, still Idle) only on a crypto
    // failure or if an attempt is already in progress.
    bool beginAsInitiator(const TrustKeyPair& ourIdentity, uint32_t nowMs, TrustHelloMessage& outMessage);

    // Handles an incoming trust.pair.begin. If Idle, this makes us the responder (returns our own
    // hello to send back via outReplyMessage, moves to AwaitingApproval). If AwaitingPeerHello,
    // this completes our own initiator exchange (outReplyMessage untouched -- nothing new to
    // send). Returns false (state -> Cancelled) if the peer's self-signature doesn't verify, or
    // if an attempt with a different peer is already past Idle.
    bool onPeerHello(const TrustHelloMessage& peerHello, const TrustKeyPair& ourIdentity, uint32_t nowMs,
                     TrustHelloMessage& outReplyMessage, bool& outHasReply);

    // Valid only in AwaitingApproval; fills the fingerprint/code to show the user.
    bool currentOutcome(TrustPairingOutcome& out) const;

    // The human tapped Confirm. Valid only in AwaitingApproval. Moves to Committed immediately if
    // the peer already confirmed first, otherwise to AwaitingPeerConfirm. Returns the
    // trust.pair.confirm signature bytes to send.
    bool confirmLocally(uint32_t nowMs, TrustSignature& outConfirmSignature);

    // An incoming trust.pair.confirm carrying the peer's signature over the shared
    // transcriptHash, verified against the peer's static key captured from their hello. Valid in
    // AwaitingApproval (peer confirmed before us -- remembered so our own confirmLocally() commits
    // right away) or AwaitingPeerConfirm (we confirmed first -- this commits now). Returns false
    // (state -> Cancelled) on a bad signature.
    bool onPeerConfirm(const TrustSignature& peerConfirmSignature, uint32_t nowMs);

    // The human tapped Deny, or the caller is cancelling for another reason. Always succeeds.
    // outShouldSendCancel is true if a trust.pair.cancel should be sent to the peer (false if we
    // were already Idle/Cancelled, so there is no peer to notify).
    void cancel(bool& outShouldSendCancel);

    // Call once per main-loop tick. Moves to Cancelled if more than timeoutMs has elapsed since
    // the state last changed. Returns true if it just timed out on this call.
    bool tick(uint32_t nowMs, uint32_t timeoutMs = 60000);

    const TrustDerivedKeys& derivedKeys() const { return derivedKeys_; }
    const TrustHelloMessage& peerHello() const { return peerHello_; }
    TrustPairingState state() const { return state_; }

    void reset();

private:
    ITrustCrypto& crypto_;
    TrustPairingState state_ = TrustPairingState::Idle;
    TrustKeyPair ourIdentity_{};
    TrustKeyPair ourEphemeral_{};
    TrustNonce ourNonce_{};
    TrustHelloMessage ourHello_{};
    TrustHelloMessage peerHello_{};
    TrustDerivedKeys derivedKeys_{};
    bool localConfirmed_ = false;
    bool peerConfirmed_ = false;
    uint32_t lastChangeMs_ = 0;
};

}  // namespace esplink
```

- [ ] **Step 2: Write `TrustPairingSession.cpp`**

```cpp
#include "TrustPairingSession.h"

namespace esplink {

bool TrustPairingSession::beginAsInitiator(const TrustKeyPair& ourIdentity, uint32_t nowMs,
                                          TrustHelloMessage& outMessage) {
    if (state_ != TrustPairingState::Idle) return false;
    if (!buildTrustHello(crypto_, ourIdentity, ourEphemeral_, ourNonce_, ourHello_)) return false;

    ourIdentity_ = ourIdentity;
    state_ = TrustPairingState::AwaitingPeerHello;
    lastChangeMs_ = nowMs;
    outMessage = ourHello_;
    return true;
}

bool TrustPairingSession::onPeerHello(const TrustHelloMessage& peerHello, const TrustKeyPair& ourIdentity,
                                     uint32_t nowMs, TrustHelloMessage& outReplyMessage, bool& outHasReply) {
    outHasReply = false;

    if (state_ == TrustPairingState::Idle) {
        // We're the responder: nothing is trusted yet, so this only checks self-consistency.
        if (!verifyTrustHello(crypto_, peerHello.staticPublicKey, peerHello)) {
            reset();
            return false;
        }
        if (!buildTrustHello(crypto_, ourIdentity, ourEphemeral_, ourNonce_, ourHello_)) {
            reset();
            return false;
        }
        ourIdentity_ = ourIdentity;
        peerHello_ = peerHello;
        if (!deriveFromHellos(crypto_, ourIdentity_, ourEphemeral_, ourNonce_, peerHello_, derivedKeys_)) {
            reset();
            return false;
        }
        state_ = TrustPairingState::AwaitingApproval;
        lastChangeMs_ = nowMs;
        outReplyMessage = ourHello_;
        outHasReply = true;
        return true;
    }

    if (state_ == TrustPairingState::AwaitingPeerHello) {
        // We're the initiator: this is the peer's reply to our own hello.
        if (!verifyTrustHello(crypto_, peerHello.staticPublicKey, peerHello)) {
            reset();
            return false;
        }
        peerHello_ = peerHello;
        if (!deriveFromHellos(crypto_, ourIdentity_, ourEphemeral_, ourNonce_, peerHello_, derivedKeys_)) {
            reset();
            return false;
        }
        state_ = TrustPairingState::AwaitingApproval;
        lastChangeMs_ = nowMs;
        return true;
    }

    return false;  // an attempt is already past Idle
}

bool TrustPairingSession::currentOutcome(TrustPairingOutcome& out) const {
    if (state_ != TrustPairingState::AwaitingApproval) return false;
    out.peerStaticPublicKey = peerHello_.staticPublicKey;
    out.numericCode = derivedKeys_.numericCode;
    return true;
}

bool TrustPairingSession::confirmLocally(uint32_t nowMs, TrustSignature& outConfirmSignature) {
    if (state_ != TrustPairingState::AwaitingApproval) return false;
    if (!crypto_.sign(ourIdentity_.privateKey, derivedKeys_.transcriptHash.data(), derivedKeys_.transcriptHash.size(),
                      outConfirmSignature)) {
        return false;
    }
    localConfirmed_ = true;
    state_ = peerConfirmed_ ? TrustPairingState::Committed : TrustPairingState::AwaitingPeerConfirm;
    lastChangeMs_ = nowMs;
    return true;
}

bool TrustPairingSession::onPeerConfirm(const TrustSignature& peerConfirmSignature, uint32_t nowMs) {
    if (state_ != TrustPairingState::AwaitingApproval && state_ != TrustPairingState::AwaitingPeerConfirm) {
        return false;
    }
    if (!crypto_.verify(peerHello_.staticPublicKey, derivedKeys_.transcriptHash.data(),
                        derivedKeys_.transcriptHash.size(), peerConfirmSignature)) {
        reset();
        return false;
    }
    peerConfirmed_ = true;
    lastChangeMs_ = nowMs;
    if (localConfirmed_) state_ = TrustPairingState::Committed;
    return true;
}

void TrustPairingSession::cancel(bool& outShouldSendCancel) {
    outShouldSendCancel = state_ != TrustPairingState::Idle && state_ != TrustPairingState::Cancelled;
    state_ = TrustPairingState::Cancelled;
}

bool TrustPairingSession::tick(uint32_t nowMs, uint32_t timeoutMs) {
    if (state_ == TrustPairingState::Idle || state_ == TrustPairingState::Committed ||
        state_ == TrustPairingState::Cancelled) {
        return false;
    }
    if (nowMs - lastChangeMs_ < timeoutMs) return false;
    state_ = TrustPairingState::Cancelled;
    return true;
}

void TrustPairingSession::reset() {
    state_ = TrustPairingState::Idle;
    localConfirmed_ = false;
    peerConfirmed_ = false;
    derivedKeys_ = TrustDerivedKeys{};
    peerHello_ = TrustHelloMessage{};
    ourHello_ = TrustHelloMessage{};
}

}  // namespace esplink
```

- [ ] **Step 3: Add tests to `test/test_native/test_main.cpp`**

```cpp
#include "TrustPairingSession.h"

void test_trust_pairing_happy_path_both_confirm_first() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair aliceIdentity, bobIdentity;
    crypto.generateKeyPair(aliceIdentity);
    crypto.generateKeyPair(bobIdentity);

    esplink::TrustPairingSession alice(crypto);
    esplink::TrustPairingSession bob(crypto);

    esplink::TrustHelloMessage aliceHello;
    TEST_ASSERT_TRUE(alice.beginAsInitiator(aliceIdentity, 1000, aliceHello));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingPeerHello == alice.state());

    esplink::TrustHelloMessage bobHello;
    bool bobHasReply = false;
    TEST_ASSERT_TRUE(bob.onPeerHello(aliceHello, bobIdentity, 1001, bobHello, bobHasReply));
    TEST_ASSERT_TRUE(bobHasReply);
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingApproval == bob.state());

    esplink::TrustHelloMessage unusedReply;
    bool aliceHasReply = false;
    TEST_ASSERT_TRUE(alice.onPeerHello(bobHello, aliceIdentity, 1002, unusedReply, aliceHasReply));
    TEST_ASSERT_FALSE(aliceHasReply);
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingApproval == alice.state());

    esplink::TrustPairingOutcome aliceOutcome, bobOutcome;
    TEST_ASSERT_TRUE(alice.currentOutcome(aliceOutcome));
    TEST_ASSERT_TRUE(bob.currentOutcome(bobOutcome));
    TEST_ASSERT_EQUAL_UINT32(aliceOutcome.numericCode, bobOutcome.numericCode);  // same code both screens

    esplink::TrustSignature aliceConfirm, bobConfirm;
    TEST_ASSERT_TRUE(alice.confirmLocally(1003, aliceConfirm));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingPeerConfirm == alice.state());
    TEST_ASSERT_TRUE(bob.confirmLocally(1004, bobConfirm));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingPeerConfirm == bob.state());

    TEST_ASSERT_TRUE(alice.onPeerConfirm(bobConfirm, 1005));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Committed == alice.state());
    TEST_ASSERT_TRUE(bob.onPeerConfirm(aliceConfirm, 1006));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Committed == bob.state());

    TEST_ASSERT_EQUAL_UINT8_ARRAY(alice.derivedKeys().lmk.data(), bob.derivedKeys().lmk.data(),
                                 alice.derivedKeys().lmk.size());
}

void test_trust_pairing_peer_confirms_before_us() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair aliceIdentity, bobIdentity;
    crypto.generateKeyPair(aliceIdentity);
    crypto.generateKeyPair(bobIdentity);

    esplink::TrustPairingSession alice(crypto);
    esplink::TrustPairingSession bob(crypto);
    esplink::TrustHelloMessage aliceHello, bobHello, unused;
    bool hasReply = false;
    alice.beginAsInitiator(aliceIdentity, 0, aliceHello);
    bob.onPeerHello(aliceHello, bobIdentity, 0, bobHello, hasReply);
    alice.onPeerHello(bobHello, aliceIdentity, 0, unused, hasReply);

    esplink::TrustSignature bobConfirm;
    TEST_ASSERT_TRUE(bob.confirmLocally(0, bobConfirm));
    TEST_ASSERT_TRUE(alice.onPeerConfirm(bobConfirm, 0));
    // Alice hasn't tapped Confirm yet -- still waiting on the local human, not committed.
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingApproval == alice.state());

    esplink::TrustSignature aliceConfirm;
    TEST_ASSERT_TRUE(alice.confirmLocally(0, aliceConfirm));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Committed == alice.state());  // peer already confirmed
}

void test_trust_pairing_cancel_and_timeout() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair identity;
    crypto.generateKeyPair(identity);

    esplink::TrustPairingSession session(crypto);
    esplink::TrustHelloMessage hello;
    session.beginAsInitiator(identity, 0, hello);

    bool shouldSend = false;
    session.cancel(shouldSend);
    TEST_ASSERT_TRUE(shouldSend);
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Cancelled == session.state());

    session.reset();
    session.beginAsInitiator(identity, 0, hello);
    TEST_ASSERT_FALSE(session.tick(30000));   // well under the 60s default timeout
    TEST_ASSERT_TRUE(session.tick(60001));    // past it
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Cancelled == session.state());
}

void test_trust_pairing_rejects_bad_signature() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair identity;
    crypto.generateKeyPair(identity);

    esplink::TrustPairingSession responder(crypto);
    esplink::TrustHelloMessage forged;
    forged.staticPublicKey.fill(0xAA);
    forged.ephemeralPublicKey.fill(0xBB);
    forged.nonce.fill(0xCC);
    forged.signature.fill(0x00);  // does not match FakeTrustCrypto's deterministic MAC

    esplink::TrustHelloMessage reply;
    bool hasReply = false;
    TEST_ASSERT_FALSE(responder.onPeerHello(forged, identity, 0, reply, hasReply));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Idle == responder.state());  // reset(), not stuck Cancelled
}
```

Register the four new tests via `RUN_TEST(...)`.

- [ ] **Step 4: Run the native test suite and verify it passes**

Run: `pio test -e native`
Expected: all tests pass, including the four new `test_trust_pairing_*` tests.

- [ ] **Step 5: Commit**

```bash
git add lib/EspLinkCore/src/TrustPairingSession.h lib/EspLinkCore/src/TrustPairingSession.cpp \
        test/test_native/test_main.cpp
git commit -m "feat(trust): add pairing handshake state machine"
```

---

## Task 4: ESP32 crypto implementation (mbedTLS)

**Files:**
- Create: `src/TrustCrypto.h`
- Create: `src/TrustCrypto.cpp`

**Interfaces:**
- Consumes: `ITrustCrypto`, `TrustKeyPair`, `TrustNonce`, `TrustSignature`, `TrustPublicKey`, `TrustPrivateKey`, `TrustHash`, `TrustDerivedKeys` (Task 1).
- Produces: `esplink::TrustCrypto`, a concrete `ITrustCrypto` — `bool begin(std::string& error)` seeds the RNG once at startup; every `ITrustCrypto` method is implemented for real. Tasks 6–8 construct one instance and pass it to `TrustPairingSession`/reconnect logic.

This task is ESP32-only (mbedTLS + `esp_random`), so it is not natively unit-tested — its correctness is exercised by the manual hardware-validation checklist in the spec (§6) once Tasks 7–8 wire it in, plus a one-time self-test at boot (Step 4 below) that catches a broken build immediately from the serial log instead of silently failing on first real pairing attempt.

- [ ] **Step 1: Write `TrustCrypto.h`**

```cpp
#pragma once

#include <string>

#include "TrustCryptoPort.h"

namespace esplink {

// mbedTLS-backed ITrustCrypto for the ESP32 build (bundled with the Arduino core -- no new
// PlatformIO lib_deps entry). P-256 (secp256r1) for both ECDSA and ECDH; HKDF-SHA256 for
// derivation, built from mbedtls_md's HMAC-SHA256 per RFC 5869 (mbedTLS's own mbedtls_hkdf.h is
// not guaranteed present on every ESP-IDF/mbedTLS version this project has built against, so this
// implements HKDF directly from the always-present HMAC primitive instead of depending on it).
class TrustCrypto : public ITrustCrypto {
public:
    // Seeds the internal CTR-DRBG from the ESP32 hardware RNG (esp_random()). Call once at
    // startup before any other method; returns false only if mbedTLS's entropy/DRBG setup fails.
    bool begin(std::string& error);

    bool generateKeyPair(TrustKeyPair& out) const override;
    bool generateNonce(TrustNonce& out) const override;
    bool sign(const TrustPrivateKey& privateKey, const uint8_t* message, std::size_t messageLength,
              TrustSignature& outSignature) const override;
    bool verify(const TrustPublicKey& publicKey, const uint8_t* message, std::size_t messageLength,
                const TrustSignature& signature) const override;
    bool sha256(const uint8_t* data, std::size_t length, TrustHash& out) const override;
    bool deriveSessionKeys(const TrustPrivateKey& ourEphemeralPrivateKey, const TrustPublicKey& ourEphemeralPublicKey,
                           const TrustPublicKey& ourStaticPublicKey, const TrustNonce& ourNonce,
                           const TrustPublicKey& peerEphemeralPublicKey, const TrustPublicKey& peerStaticPublicKey,
                           const TrustNonce& peerNonce, TrustDerivedKeys& out) const override;

    // Runs a full self-check (keygen, sign/verify round trip, ECDH agreement between two
    // generated keypairs, HKDF determinism) using only this instance's own RNG. Logged once from
    // main.cpp's setup() so a broken mbedTLS integration shows up in the serial log immediately.
    bool selfTest(std::string& error);

private:
    bool rngReady_ = false;
};

}  // namespace esplink
```

- [ ] **Step 2: Write `TrustCrypto.cpp`**

```cpp
#include "TrustCrypto.h"

#include <esp_random.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>

#include <cstring>

namespace esplink {

namespace {
mbedtls_entropy_context g_entropy;
mbedtls_ctr_drbg_context g_ctrDrbg;

int espRandomCallback(void*, unsigned char* output, std::size_t length) {
    esp_fill_random(output, length);
    return 0;
}

bool mpiToFixedBytes(const mbedtls_mpi& value, uint8_t* out, std::size_t outLength) {
    return mbedtls_mpi_write_binary(&value, out, outLength) == 0;
}

// HKDF-SHA256 (RFC 5869) built from mbedtls_md's HMAC, since mbedtls_hkdf.h is not guaranteed
// present on every mbedTLS version this project has built against.
bool hkdfSha256(const uint8_t* salt, std::size_t saltLength, const uint8_t* ikm, std::size_t ikmLength,
               const uint8_t* info, std::size_t infoLength, uint8_t* out, std::size_t outLength) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr) return false;
    constexpr std::size_t kHashLen = 32;

    uint8_t prk[kHashLen];
    if (mbedtls_md_hmac(mdInfo, salt, saltLength, ikm, ikmLength, prk) != 0) return false;

    uint8_t previous[kHashLen] = {};
    std::size_t previousLength = 0;
    std::size_t produced = 0;
    uint8_t counter = 1;
    while (produced < outLength) {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        if (mbedtls_md_setup(&ctx, mdInfo, 1) != 0 || mbedtls_md_hmac_starts(&ctx, prk, kHashLen) != 0 ||
            (previousLength > 0 && mbedtls_md_hmac_update(&ctx, previous, previousLength) != 0) ||
            mbedtls_md_hmac_update(&ctx, info, infoLength) != 0 ||
            mbedtls_md_hmac_update(&ctx, &counter, 1) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }
        uint8_t block[kHashLen];
        if (mbedtls_md_hmac_finish(&ctx, block) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }
        mbedtls_md_free(&ctx);

        const std::size_t take = std::min(kHashLen, outLength - produced);
        std::memcpy(out + produced, block, take);
        std::memcpy(previous, block, kHashLen);
        previousLength = kHashLen;
        produced += take;
        ++counter;
    }
    return true;
}
}  // namespace

bool TrustCrypto::begin(std::string& error) {
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctrDrbg);
    const unsigned char customSeed[1] = {0};
    if (mbedtls_ctr_drbg_seed(&g_ctrDrbg, espRandomCallback, nullptr, customSeed, sizeof(customSeed)) != 0) {
        error = "mbedtls_ctr_drbg_seed failed";
        return false;
    }
    rngReady_ = true;
    return true;
}

bool TrustCrypto::generateKeyPair(TrustKeyPair& out) const {
    if (!rngReady_) return false;
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi privateScalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&privateScalar);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_gen_keypair(&group, &privateScalar, &point, mbedtls_ctr_drbg_random, &g_ctrDrbg) == 0 &&
              mpiToFixedBytes(privateScalar, out.privateKey.data(), out.privateKey.size());
    std::size_t written = 0;
    if (ok) {
        ok = mbedtls_ecp_point_write_binary(&group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED, &written,
                                            out.publicKey.data(), out.publicKey.size()) == 0 &&
             written == out.publicKey.size();
    }

    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool TrustCrypto::generateNonce(TrustNonce& out) const {
    if (!rngReady_) return false;
    return mbedtls_ctr_drbg_random(&g_ctrDrbg, out.data(), out.size()) == 0;
}

bool TrustCrypto::sign(const TrustPrivateKey& privateKey, const uint8_t* message, std::size_t messageLength,
                      TrustSignature& outSignature) const {
    if (!rngReady_) return false;
    TrustHash digest{};
    if (!sha256(message, messageLength, digest)) return false;

    mbedtls_ecp_group group;
    mbedtls_mpi privateScalar, r, s;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&privateScalar);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_mpi_read_binary(&privateScalar, privateKey.data(), privateKey.size()) == 0 &&
              mbedtls_ecdsa_sign(&group, &r, &s, &privateScalar, digest.data(), digest.size(),
                                 mbedtls_ctr_drbg_random, &g_ctrDrbg) == 0 &&
              mpiToFixedBytes(r, outSignature.data(), 32) && mpiToFixedBytes(s, outSignature.data() + 32, 32);

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool TrustCrypto::verify(const TrustPublicKey& publicKey, const uint8_t* message, std::size_t messageLength,
                        const TrustSignature& signature) const {
    TrustHash digest{};
    if (!sha256(message, messageLength, digest)) return false;

    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_point_read_binary(&group, &point, publicKey.data(), publicKey.size()) == 0 &&
              mbedtls_mpi_read_binary(&r, signature.data(), 32) == 0 &&
              mbedtls_mpi_read_binary(&s, signature.data() + 32, 32) == 0 &&
              mbedtls_ecdsa_verify(&group, digest.data(), digest.size(), &point, &r, &s) == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return ok;
}

bool TrustCrypto::sha256(const uint8_t* data, std::size_t length, TrustHash& out) const {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return mdInfo != nullptr && mbedtls_md(mdInfo, data, length, out.data()) == 0;
}

bool TrustCrypto::deriveSessionKeys(const TrustPrivateKey& ourEphemeralPrivateKey,
                                   const TrustPublicKey& /*ourEphemeralPublicKey*/,
                                   const TrustPublicKey& ourStaticPublicKey, const TrustNonce& ourNonce,
                                   const TrustPublicKey& peerEphemeralPublicKey,
                                   const TrustPublicKey& peerStaticPublicKey, const TrustNonce& peerNonce,
                                   TrustDerivedKeys& out) const {
    if (!rngReady_) return false;

    mbedtls_ecp_group group;
    mbedtls_ecp_point peerPoint, sharedPoint;
    mbedtls_mpi privateScalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&peerPoint);
    mbedtls_ecp_point_init(&sharedPoint);
    mbedtls_mpi_init(&privateScalar);

    bool ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_ecp_point_read_binary(&group, &peerPoint, peerEphemeralPublicKey.data(),
                                            peerEphemeralPublicKey.size()) == 0 &&
              mbedtls_mpi_read_binary(&privateScalar, ourEphemeralPrivateKey.data(),
                                      ourEphemeralPrivateKey.size()) == 0 &&
              mbedtls_ecp_mul(&group, &sharedPoint, &privateScalar, &peerPoint, mbedtls_ctr_drbg_random,
                              &g_ctrDrbg) == 0;

    std::array<uint8_t, 32> sharedSecretX{};
    if (ok) ok = mpiToFixedBytes(sharedPoint.X, sharedSecretX.data(), sharedSecretX.size());

    mbedtls_mpi_free(&privateScalar);
    mbedtls_ecp_point_free(&sharedPoint);
    mbedtls_ecp_point_free(&peerPoint);
    mbedtls_ecp_group_free(&group);
    if (!ok) return false;

    // Canonicalize by comparing the two static public keys so both sides hash identically
    // regardless of which one is "ours" -- see the ITrustCrypto::deriveSessionKeys contract.
    const bool ourStaticIsSmaller =
        std::memcmp(ourStaticPublicKey.data(), peerStaticPublicKey.data(), kTrustPublicKeyBytes) < 0;
    const TrustPublicKey& firstStatic = ourStaticIsSmaller ? ourStaticPublicKey : peerStaticPublicKey;
    const TrustPublicKey& secondStatic = ourStaticIsSmaller ? peerStaticPublicKey : ourStaticPublicKey;
    const TrustNonce& firstNonce = ourStaticIsSmaller ? ourNonce : peerNonce;
    const TrustNonce& secondNonce = ourStaticIsSmaller ? peerNonce : ourNonce;

    std::array<uint8_t, kTrustNonceBytes * 2> salt{};
    std::memcpy(salt.data(), firstNonce.data(), kTrustNonceBytes);
    std::memcpy(salt.data() + kTrustNonceBytes, secondNonce.data(), kTrustNonceBytes);

    static constexpr char kInfoPrefix[] = "esbg-trust-v1";
    std::array<uint8_t, sizeof(kInfoPrefix) - 1 + kTrustPublicKeyBytes * 2> info{};
    std::size_t offset = 0;
    std::memcpy(info.data(), kInfoPrefix, sizeof(kInfoPrefix) - 1);
    offset += sizeof(kInfoPrefix) - 1;
    std::memcpy(info.data() + offset, firstStatic.data(), kTrustPublicKeyBytes);
    offset += kTrustPublicKeyBytes;
    std::memcpy(info.data() + offset, secondStatic.data(), kTrustPublicKeyBytes);

    // 16 bytes LMK + 4 bytes numeric-code seed + 32 bytes transcript hash = 52 bytes.
    std::array<uint8_t, kTrustLmkBytes + 4 + kTrustHashBytes> hkdfOutput{};
    if (!hkdfSha256(salt.data(), salt.size(), sharedSecretX.data(), sharedSecretX.size(), info.data(), info.size(),
                    hkdfOutput.data(), hkdfOutput.size())) {
        return false;
    }

    std::memcpy(out.lmk.data(), hkdfOutput.data(), kTrustLmkBytes);
    const uint8_t* codeBytes = hkdfOutput.data() + kTrustLmkBytes;
    out.numericCode = (static_cast<uint32_t>(codeBytes[0]) << 24 | static_cast<uint32_t>(codeBytes[1]) << 16 |
                       static_cast<uint32_t>(codeBytes[2]) << 8 | codeBytes[3]) %
                      1000000u;
    std::memcpy(out.transcriptHash.data(), hkdfOutput.data() + kTrustLmkBytes + 4, kTrustHashBytes);
    return true;
}

bool TrustCrypto::selfTest(std::string& error) {
    TrustKeyPair alice, bob;
    if (!generateKeyPair(alice) || !generateKeyPair(bob)) {
        error = "keygen failed";
        return false;
    }
    const uint8_t message[] = {1, 2, 3, 4, 5};
    TrustSignature signature{};
    if (!sign(alice.privateKey, message, sizeof(message), signature)) {
        error = "sign failed";
        return false;
    }
    if (!verify(alice.publicKey, message, sizeof(message), signature)) {
        error = "verify of a valid signature failed";
        return false;
    }
    if (verify(bob.publicKey, message, sizeof(message), signature)) {
        error = "verify accepted a signature from the wrong key";
        return false;
    }

    TrustNonce nonceA{}, nonceB{};
    generateNonce(nonceA);
    generateNonce(nonceB);
    TrustKeyPair ephemeralA, ephemeralB;
    generateKeyPair(ephemeralA);
    generateKeyPair(ephemeralB);
    TrustDerivedKeys derivedA, derivedB;
    if (!deriveSessionKeys(ephemeralA.privateKey, ephemeralA.publicKey, alice.publicKey, nonceA, ephemeralB.publicKey,
                          bob.publicKey, nonceB, derivedA) ||
        !deriveSessionKeys(ephemeralB.privateKey, ephemeralB.publicKey, bob.publicKey, nonceB, ephemeralA.publicKey,
                          alice.publicKey, nonceA, derivedB)) {
        error = "deriveSessionKeys failed";
        return false;
    }
    if (derivedA.lmk != derivedB.lmk || derivedA.numericCode != derivedB.numericCode) {
        error = "deriveSessionKeys is not symmetric between the two sides";
        return false;
    }
    return true;
}

}  // namespace esplink
```

- [ ] **Step 3: Build the ESP32 firmware and verify it compiles**

Run: `pio run -e esp32dev`
Expected: build succeeds (mbedTLS headers resolve from the bundled ESP-IDF component; no new `lib_deps` needed).

- [ ] **Step 4: Wire the self-test into `src/main.cpp`'s `setup()`**, right after `application.begin(error)` succeeds (so a broken build is visible on the very first boot, before anything depends on it):

```cpp
esplink::TrustCrypto trustCrypto;
std::string trustCryptoError;
if (!trustCrypto.begin(trustCryptoError) || !trustCrypto.selfTest(trustCryptoError)) {
    Serial.printf("{\"event\":\"trust_crypto_selftest_failed\",\"message\":\"%s\"}\n", trustCryptoError.c_str());
} else {
    Serial.printf("{\"event\":\"trust_crypto_selftest_passed\"}\n");
}
```

(`trustCrypto` becomes a file-scope global alongside `espNowEndpoint`/`gatewayRelay` etc. in Task 7/8 — this step just proves the module works before anything else depends on it.)

- [ ] **Step 5: Flash the device and confirm the self-test log line**

Run: `pio run -e esp32dev -t upload -t monitor` (or your existing flash workflow)
Expected: serial log shows `{"event":"trust_crypto_selftest_passed"}` shortly after boot.

- [ ] **Step 6: Commit**

```bash
git add src/TrustCrypto.h src/TrustCrypto.cpp src/main.cpp
git commit -m "feat(trust): add mbedTLS-backed ECDSA/ECDH/HKDF implementation with boot self-test"
```

---

## Task 5: ESP32 persistence (trust records + device identity)

**Files:**
- Create: `include/TrustConfigStore.h`
- Create: `src/TrustConfigStore.cpp`

**Interfaces:**
- Consumes: `TrustStore`, `TrustRecord`, `TrustRole` (Task 2), `ITrustCrypto`, `TrustKeyPair` (Task 1).
- Produces: `TrustConfigStore` — `begin(ITrustCrypto&, std::string& error)` (loads or generates the device identity, loads persisted records into an owned `TrustStore`), `identity()`, `store()`/`mutableStore()`, `securePairingEnabled()`/`setSecurePairingEnabled()`, `persistRecords(std::string& error)`. Tasks 7–9 depend on this for identity + persistence + the Settings toggle.

- [ ] **Step 1: Write `include/TrustConfigStore.h`**

```cpp
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

    bool securePairingEnabled() const { return securePairingEnabled_; }
    bool setSecurePairingEnabled(bool value, std::string& error);

    // Adds a record and persists it. Fails (nothing changed) if the store is full or the record
    // already exists (see TrustStore::add), or if the write fails.
    bool addRecord(const esplink::TrustRecord& record, std::string& error);

    // Removes a record by static key and persists the change. Returns true only if a record was
    // actually removed and the write succeeded (removed-but-write-failed re-adds the record and
    // returns false, keeping in-memory and on-disk state consistent).
    bool forgetRecord(const esplink::TrustPublicKey& key, std::string& error);

private:
    bool loadIdentity(esplink::ITrustCrypto& crypto, std::string& error);
    bool saveIdentity(std::string& error) const;
    bool loadRecords();
    bool saveRecords(std::string& error) const;

    esplink::TrustKeyPair identity_{};
    esplink::TrustStore store_;
    bool securePairingEnabled_ = false;
};
```

- [ ] **Step 2: Write `src/TrustConfigStore.cpp`**

```cpp
#include "TrustConfigStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "EspBarcodeCore.h"  // bytesToBase64/bytesFromBase64

using namespace esplink;

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
```

- [ ] **Step 3: Build the ESP32 firmware and verify it compiles**

Run: `pio run -e esp32dev`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add include/TrustConfigStore.h src/TrustConfigStore.cpp
git commit -m "feat(trust): add LittleFS/NVS-backed trust persistence"
```

---

## Task 6: `EspNowEndpoint` enforcement + pairing

**Files:**
- Modify: `src/EspNowEndpoint.h`
- Modify: `src/EspNowEndpoint.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `TrustCrypto` (Task 4), `TrustConfigStore` (Task 5), `TrustPairingSession`, `TrustHelloMessage`, `TrustSignature` (Tasks 1–3).
- Produces: `EspNowEndpoint::beginPairing(targetMac)`, `confirmPairing()`, `denyPairing()`, `pairingStatus()` (for the on-device UI, Task 9), plus enforcement inside the existing receive path.

- [ ] **Step 1: Add trust wiring to `src/EspNowEndpoint.h`** — new includes (`TrustConfigStore.h`, `TrustPairingSession.h`), new public API, and new private state, inserted alongside the existing gateway-discovery members (the class comment block above line 33 already documents that section):

```cpp
// New includes near the top:
#include "TrustConfigStore.h"
#include "TrustPairingSession.h"

// New public methods (alongside gatewayLinkStatus()):
enum class TrustPairingUiState : uint8_t { Idle, Discovering, AwaitingApproval, Committed, Cancelled };
struct TrustPairingUiStatus {
    TrustPairingUiState state = TrustPairingUiState::Idle;
    std::string peerFingerprint;
    uint32_t numericCode = 0;
};

// Starts pairing with a specific MAC seen via the existing discovery ping/pong (see
// gatewayLinkStatus()/lastGatewayId_ -- the on-device UI, Task 8, offers this as "the
// currently-discovered gateway" since EspNowEndpoint is inherently 1:1 with a single gateway at a
// time).
bool beginPairing(const std::array<uint8_t, 6>& targetMac);
void confirmPairing();
void denyPairing();
TrustPairingUiStatus pairingStatus() const;
const esplink::TrustStore& trustedPeers() const { return trustConfig_.store(); }
// Looks up a record by its displayed fingerprint (same format trustFingerprint() produces) and
// forgets it. Returns false if no record matches. Used by the on-device Trust screen's "Forget"
// button (Task 8), which only has the fingerprint string shown on screen, not the raw public key.
bool forgetByFingerprint(const std::string& fingerprint);
// One fingerprint string per trusted record, same order as trustedPeers() -- the on-device Trust
// screen (Task 8) renders this directly instead of recomputing fingerprints itself, since that
// needs trustCrypto_'s sha256(), which is private to this class.
std::vector<std::string> fingerprintList() const;
bool setSecurePairingEnabled(bool value, std::string& error) { return trustConfig_.setSecurePairingEnabled(value, error); }
bool securePairingEnabled() const { return trustConfig_.securePairingEnabled(); }

// New private members (alongside the gateway-discovery state block):
TrustConfigStore trustConfig_;
esplink::TrustCrypto trustCrypto_;
esplink::TrustPairingSession trustPairing_{trustCrypto_};
std::array<uint8_t, 6> pairingTargetMac_{};
bool pairingIsInitiator_ = false;
void handleTrustMessage(const uint8_t* fromMac, JsonObjectConst wrapper);
void sendTrustHello(const std::array<uint8_t, 6>& toMac, const TrustHelloMessage& hello);
void sendTrustConfirm(const std::array<uint8_t, 6>& toMac, const TrustSignature& signature);
void sendTrustCancel(const std::array<uint8_t, 6>& toMac);
bool ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac);
bool upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac, const std::array<uint8_t, 16>& lmk);
```

- [ ] **Step 2: Wire `begin()` to initialize trust state** — in `EspNowEndpoint::begin`, after the existing broadcast-peer setup succeeds, add:

```cpp
std::string trustError;
if (!trustCrypto_.begin(trustError) || !trustConfig_.begin(trustCrypto_, trustError)) {
    error = "trust init failed: " + trustError;
    return false;
}
```

- [ ] **Step 3: Change `enqueueReceived`/`processDatagram`/`processMessage` to carry the sender MAC** — today `enqueueReceived` explicitly discards it (`(void)mac;`); add `std::array<uint8_t, 6> mac` to `RxDatagram`, capture it in `enqueueReceived` (mirroring how `GatewayRelay::RxDatagram` already does this), and thread it through `processDatagram(datagram)` → `processMessage(mac, envelope, body)` so `ServiceId::Trust` messages know who sent them:

```cpp
// RxDatagram gains:
std::array<uint8_t, 6> mac{};

// enqueueReceived: replace `(void)mac;` with
memcpy(slot.mac.data(), mac, slot.mac.size());

// processDatagram passes datagram.mac through to processMessage; processMessage's signature
// becomes processMessage(const std::array<uint8_t,6>& fromMac, const MessageEnvelope&, const
// std::vector<uint8_t>&), and its ServiceId::Gateway branch is joined by:
if (envelope.serviceId == ServiceId::Trust) {
    handleTrustMessage(fromMac.data(), wrapper);
    return;
}
```

- [ ] **Step 4: Implement enforcement in `processMessage`** — for any `ServiceId` other than `Trust`/`Gateway`, if `trustConfig_.securePairingEnabled()` is true, drop the message unless `fromMac` matches a record in `trustConfig_.store()`:

```cpp
if (trustConfig_.securePairingEnabled() && envelope.serviceId != ServiceId::Trust &&
    envelope.serviceId != ServiceId::Gateway) {
    std::array<uint8_t, 6> macArray{};
    std::memcpy(macArray.data(), fromMac.data(), 6);
    if (trustConfig_.store().findByMac(macArray) == nullptr) return;  // not a trusted peer: drop
}
```

- [ ] **Step 5: Implement `handleTrustMessage`, `sendTrustHello/Confirm/Cancel`, `ensureUnencryptedPeer`, `upgradeToEncryptedPeer`, `beginPairing`, `confirmPairing`, `denyPairing`, `pairingStatus`** in `EspNowEndpoint.cpp`:

```cpp
namespace {
bool decodeHelloJson(JsonObjectConst body, esplink::TrustHelloMessage& out) {
    std::vector<uint8_t> bytes;
    if (!bytesFromBase64(body["staticPubKey"] | "", bytes) || bytes.size() != esplink::kTrustPublicKeyBytes) {
        return false;
    }
    std::memcpy(out.staticPublicKey.data(), bytes.data(), bytes.size());
    if (!bytesFromBase64(body["ephemeralPubKey"] | "", bytes) || bytes.size() != esplink::kTrustPublicKeyBytes) {
        return false;
    }
    std::memcpy(out.ephemeralPublicKey.data(), bytes.data(), bytes.size());
    if (!bytesFromBase64(body["nonce"] | "", bytes) || bytes.size() != esplink::kTrustNonceBytes) return false;
    std::memcpy(out.nonce.data(), bytes.data(), bytes.size());
    if (!bytesFromBase64(body["signature"] | "", bytes) || bytes.size() != esplink::kTrustSignatureBytes) {
        return false;
    }
    std::memcpy(out.signature.data(), bytes.data(), bytes.size());
    return true;
}

void encodeHelloJson(const esplink::TrustHelloMessage& hello, JsonObject body) {
    body["staticPubKey"] =
        bytesToBase64(std::vector<uint8_t>(hello.staticPublicKey.begin(), hello.staticPublicKey.end()));
    body["ephemeralPubKey"] =
        bytesToBase64(std::vector<uint8_t>(hello.ephemeralPublicKey.begin(), hello.ephemeralPublicKey.end()));
    body["nonce"] = bytesToBase64(std::vector<uint8_t>(hello.nonce.begin(), hello.nonce.end()));
    body["signature"] = bytesToBase64(std::vector<uint8_t>(hello.signature.begin(), hello.signature.end()));
}
}  // namespace

bool EspNowEndpoint::ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac) {
    if (esp_now_is_peer_exist(mac.data())) return true;
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;  // 0 = use the current channel, already fixed by begin()
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowEndpoint::upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac, const std::array<uint8_t, 16>& lmk) {
    esp_now_del_peer(mac.data());
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, lmk.data(), lmk.size());
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowEndpoint::beginPairing(const std::array<uint8_t, 6>& targetMac) {
    if (trustPairing_.state() != esplink::TrustPairingState::Idle) return false;
    if (!ensureUnencryptedPeer(targetMac)) return false;

    esplink::TrustHelloMessage hello;
    if (!trustPairing_.beginAsInitiator(trustConfig_.identity(), millis(), hello)) return false;
    pairingTargetMac_ = targetMac;
    pairingIsInitiator_ = true;
    sendTrustHello(targetMac, hello);
    return true;
}

void EspNowEndpoint::confirmPairing() {
    esplink::TrustSignature signature{};
    if (!trustPairing_.confirmLocally(millis(), signature)) return;
    sendTrustConfirm(pairingTargetMac_, signature);
    if (trustPairing_.state() == esplink::TrustPairingState::Committed) {
        esplink::TrustRecord record;
        record.staticPublicKey = trustPairing_.peerHello().staticPublicKey;
        record.mac = pairingTargetMac_;
        record.peerRole = esplink::TrustRole::Gateway;  // EspNowEndpoint always pairs with a gateway
        record.pairedAtMs = millis();
        std::string persistError;
        trustConfig_.addRecord(record, persistError);
        upgradeToEncryptedPeer(pairingTargetMac_, trustPairing_.derivedKeys().lmk);
        trustPairing_.reset();
    }
}

void EspNowEndpoint::denyPairing() {
    bool shouldSend = false;
    trustPairing_.cancel(shouldSend);
    if (shouldSend) sendTrustCancel(pairingTargetMac_);
    esp_now_del_peer(pairingTargetMac_.data());
    trustPairing_.reset();
}

EspNowEndpoint::TrustPairingUiStatus EspNowEndpoint::pairingStatus() const {
    TrustPairingUiStatus status;
    esplink::TrustPairingOutcome outcome;
    switch (trustPairing_.state()) {
        case esplink::TrustPairingState::Idle:
            status.state = TrustPairingUiState::Idle;
            break;
        case esplink::TrustPairingState::AwaitingPeerHello:
            status.state = TrustPairingUiState::Discovering;
            break;
        case esplink::TrustPairingState::AwaitingApproval:
        case esplink::TrustPairingState::AwaitingPeerConfirm:
            status.state = TrustPairingUiState::AwaitingApproval;
            if (trustPairing_.currentOutcome(outcome)) {
                esplink::TrustHash hash{};
                trustCrypto_.sha256(outcome.peerStaticPublicKey.data(), outcome.peerStaticPublicKey.size(), hash);
                status.peerFingerprint = esplink::trustFingerprint(hash);
                status.numericCode = outcome.numericCode;
            }
            break;
        case esplink::TrustPairingState::Committed:
            status.state = TrustPairingUiState::Committed;
            break;
        case esplink::TrustPairingState::Cancelled:
            status.state = TrustPairingUiState::Cancelled;
            break;
    }
    return status;
}

std::vector<std::string> EspNowEndpoint::fingerprintList() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const esplink::TrustRecord* record = trustConfig_.store().at(i);
        esplink::TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        out.push_back(esplink::trustFingerprint(hash));
    }
    return out;
}

bool EspNowEndpoint::forgetByFingerprint(const std::string& fingerprint) {
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const esplink::TrustRecord* record = trustConfig_.store().at(i);
        esplink::TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        if (esplink::trustFingerprint(hash) == fingerprint) {
            std::string error;
            return trustConfig_.forgetRecord(record->staticPublicKey, error);
        }
    }
    return false;
}

void EspNowEndpoint::handleTrustMessage(const uint8_t* fromMac, JsonObjectConst wrapper) {
    std::array<uint8_t, 6> mac{};
    std::memcpy(mac.data(), fromMac, 6);
    const char* name = wrapper["name"] | "";
    JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();

    if (std::strcmp(name, "trust.pair.begin") == 0) {
        esplink::TrustHelloMessage peerHello;
        if (!decodeHelloJson(body, peerHello)) return;

        // Reconnect fast path: a peer we already trust, re-establishing a fresh session key --
        // no human approval needed (see spec §1 "Reconnect").
        const esplink::TrustRecord* existing = trustConfig_.store().findByMac(mac);
        if (existing != nullptr) {
            if (!esplink::verifyTrustHello(trustCrypto_, existing->staticPublicKey, peerHello)) return;
            esplink::TrustKeyPair ourEphemeral;
            esplink::TrustNonce ourNonce;
            esplink::TrustHelloMessage ourHello;
            if (!esplink::buildTrustHello(trustCrypto_, trustConfig_.identity(), ourEphemeral, ourNonce, ourHello)) {
                return;
            }
            esplink::TrustDerivedKeys derived;
            if (!esplink::deriveFromHellos(trustCrypto_, trustConfig_.identity(), ourEphemeral, ourNonce, peerHello,
                                          derived)) {
                return;
            }
            ensureUnencryptedPeer(mac);
            sendTrustHello(mac, ourHello);
            upgradeToEncryptedPeer(mac, derived.lmk);
            return;
        }

        if (!ensureUnencryptedPeer(mac)) return;
        esplink::TrustHelloMessage reply;
        bool hasReply = false;
        if (!trustPairing_.onPeerHello(peerHello, trustConfig_.identity(), millis(), reply, hasReply)) return;
        pairingTargetMac_ = mac;
        pairingIsInitiator_ = false;
        if (hasReply) sendTrustHello(mac, reply);
        return;
    }

    if (std::strcmp(name, "trust.pair.confirm") == 0) {
        std::vector<uint8_t> signatureBytes;
        if (!bytesFromBase64(body["confirmSignature"] | "", signatureBytes) ||
            signatureBytes.size() != esplink::kTrustSignatureBytes) {
            return;
        }
        esplink::TrustSignature signature{};
        std::memcpy(signature.data(), signatureBytes.data(), signature.size());
        if (!trustPairing_.onPeerConfirm(signature, millis())) return;
        if (trustPairing_.state() == esplink::TrustPairingState::Committed) {
            esplink::TrustRecord record;
            record.staticPublicKey = trustPairing_.peerHello().staticPublicKey;
            record.mac = mac;
            record.peerRole = esplink::TrustRole::Gateway;
            record.pairedAtMs = millis();
            std::string persistError;
            trustConfig_.addRecord(record, persistError);
            upgradeToEncryptedPeer(mac, trustPairing_.derivedKeys().lmk);
            trustPairing_.reset();
        }
        return;
    }

    if (std::strcmp(name, "trust.pair.cancel") == 0) {
        esp_now_del_peer(mac.data());
        trustPairing_.reset();
    }
}

void EspNowEndpoint::sendTrustHello(const std::array<uint8_t, 6>& toMac, const esplink::TrustHelloMessage& hello) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = "trust.pair.begin";
    encodeHelloJson(hello, wrapper["body"].to<JsonObject>());
    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    // Reuses sendEnvelope's framing, but unicast to toMac instead of the broadcast address --
    // see sendEnvelope's implementation for the envelope/fragmentation plumbing this composes
    // with (extend sendEnvelope to take a destination MAC parameter, defaulting to
    // kBroadcastAddress, rather than duplicating the fragment/send loop here).
    sendEnvelopeTo(toMac, MessageKind::Event, ServiceId::Trust, bodyBytes, 0);
}

void EspNowEndpoint::sendTrustConfirm(const std::array<uint8_t, 6>& toMac, const esplink::TrustSignature& signature) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = "trust.pair.confirm";
    wrapper["body"]["confirmSignature"] =
        bytesToBase64(std::vector<uint8_t>(signature.begin(), signature.end()));
    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(toMac, MessageKind::Event, ServiceId::Trust, bodyBytes, 0);
}

void EspNowEndpoint::sendTrustCancel(const std::array<uint8_t, 6>& toMac) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = "trust.pair.cancel";
    wrapper["body"].to<JsonObject>();
    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(toMac, MessageKind::Event, ServiceId::Trust, bodyBytes, 0);
}
```

- [ ] **Step 6: Generalize `sendEnvelope` to `sendEnvelopeTo`** — rename the existing `sendEnvelope` to take a destination `const uint8_t*` (defaulting call sites to `kBroadcastAddress`), and replace its final `esp_now_send(kBroadcastAddress, ...)` loop with `esp_now_send(destination, frame.data(), frame.size())`. Update `send()`/`sendError()`/`sendGatewayLinkEvent()`'s call sites to pass `kBroadcastAddress` explicitly, keeping their behavior unchanged, and add the new `sendEnvelopeTo(toMac, ...)` overload used by Step 5's trust senders.

- [ ] **Step 7: Wire `main.cpp`** — no changes needed beyond what Task 4 already added (the `trustCrypto`/`selfTest` block); `EspNowEndpoint` now owns its own `TrustCrypto`/`TrustConfigStore` internally, so Task 4's standalone global/self-test block in `main.cpp` can be removed once `espNowEndpoint.begin()` runs its own `trustCrypto_.begin()` + `trustConfig_.begin()` (Step 2) — replace the Task-4 self-test block with:

```cpp
std::string selfTestError;
esplink::TrustCrypto selfTestCrypto;
if (!selfTestCrypto.begin(selfTestError) || !selfTestCrypto.selfTest(selfTestError)) {
    Serial.printf("{\"event\":\"trust_crypto_selftest_failed\",\"message\":\"%s\"}\n", selfTestError.c_str());
} else {
    Serial.printf("{\"event\":\"trust_crypto_selftest_passed\"}\n");
}
```

(kept as a standalone, throwaway instance purely for the boot self-test log line, independent of `espNowEndpoint`'s own internal `trustCrypto_`.)

- [ ] **Step 8: Build the firmware and verify it compiles**

Run: `pio run -e esp32dev`
Expected: build succeeds.

- [ ] **Step 9: Manual hardware validation** (two boards, per the spec's §6 checklist): flash both, enable Secure Pairing on both from Settings (UI lands in Task 9 — until then, drive `beginPairing`/`confirmPairing` from a temporary serial/debug hook, or defer this exact step until after Task 9 and re-run it then). Confirm: pairing completes with matching numeric codes on both serial logs (`pairingStatus()`), a second `barcode.generate` from an unpaired third source is dropped once Secure Pairing is on, and rebooting both boards reconnects silently (no new approval).

- [ ] **Step 10: Commit**

```bash
git add src/EspNowEndpoint.h src/EspNowEndpoint.cpp src/main.cpp
git commit -m "feat(trust): enforce pairing and add trust.* handling to EspNowEndpoint"
```

---

## Task 7: `GatewayRelay` enforcement, pairing, and per-client routing

**Files:**
- Modify: `src/GatewayRelay.h`
- Modify: `src/GatewayRelay.cpp`

**Interfaces:**
- Consumes: same Task 1–5 types as Task 6, plus `TrustStore::kMaxRecords`/`routeId` (Task 2).
- Produces: `GatewayRelay::beginPairing(targetMac)`, `confirmPairing()`, `denyPairing()`, `pairingStatus()`, `trustedPeers()` (for on-device UI and the USB-forwarded `trust.controllers.list`), plus routed (not broadcast) `relayToEspNow` and `trust.controller.forget` handling.

- [ ] **Step 1: Confirm the real ESP-NOW encrypted-peer ceiling** — check `ESP_NOW_MAX_ENCRYPT_PEER_NUM` in the `esp_now.h` bundled with `platformio/espressif32@7.0.1`'s ESP-IDF (search the toolchain's `esp_now.h`, typically under `~/.platformio/packages/framework-arduinoespressif32/tools/esp32-arduino-libs/**/include/esp_wifi/**/esp_now.h`). If it differs from `6`, update `TrustStore::kMaxRecords` in `lib/EspLinkCore/src/TrustStore.h` (Task 2) to match, re-run `pio test -e native`, and commit that fix by itself before continuing:

```bash
git add lib/EspLinkCore/src/TrustStore.h
git commit -m "fix(trust): match TrustStore::kMaxRecords to ESP_NOW_MAX_ENCRYPT_PEER_NUM"
```

- [ ] **Step 2: Add trust wiring to `src/GatewayRelay.h`** — new includes and members mirroring Task 6's `EspNowEndpoint` additions, plus per-client routing:

```cpp
#include "TrustConfigStore.h"
#include "TrustPairingSession.h"

// New public methods (alongside stats()):
// A duplicate of EspNowEndpoint::TrustPairingUiState/TrustPairingUiStatus (Task 6 Step 1) --
// GatewayRelay and EspNowEndpoint are already independent classes with no shared base or common
// header for this kind of UI-facing value, so this follows the same "duplicate the small value
// type" choice already made for formatMac() between these two files.
enum class TrustPairingUiState : uint8_t { Idle, Discovering, AwaitingApproval, Committed, Cancelled };
struct TrustPairingUiStatus {
    TrustPairingUiState state = TrustPairingUiState::Idle;
    std::string peerFingerprint;
    uint32_t numericCode = 0;
};
bool beginPairing(const std::array<uint8_t, 6>& targetMac);
void confirmPairing();
void denyPairing();
TrustPairingUiStatus pairingStatus() const;
const esplink::TrustStore& trustedPeers() const { return trustConfig_.store(); }
bool forgetByFingerprint(const std::string& fingerprint);  // same contract as EspNowEndpoint's (Task 6)
std::vector<std::string> fingerprintList() const;          // same contract as EspNowEndpoint's (Task 6)
bool setSecurePairingEnabled(bool value, std::string& error) { return trustConfig_.setSecurePairingEnabled(value, error); }
bool securePairingEnabled() const { return trustConfig_.securePairingEnabled(); }

// New private members (alongside the discovery state):
TrustConfigStore trustConfig_;
esplink::TrustCrypto trustCrypto_;
esplink::TrustPairingSession trustPairing_{trustCrypto_};
std::array<uint8_t, 6> pairingTargetMac_{};
void handleTrustFromEspNow(const std::array<uint8_t, 6>& fromMac, JsonObjectConst wrapper);
bool handleTrustFromUsb(const HopFrameHeader& sourceHeader, const MessageEnvelope& envelope, JsonObjectConst wrapper);
bool ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac);
bool upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac, const std::array<uint8_t, 16>& lmk);
```

- [ ] **Step 3: Initialize trust state in `GatewayRelay::begin`**

```cpp
void GatewayRelay::begin(std::string deviceId) {
    deviceId_ = std::move(deviceId);
    rxBlock_.reserve(256);
    std::string trustError;
    trustCrypto_.begin(trustError);
    trustConfig_.begin(trustCrypto_, trustError);
    esp_now_register_recv_cb(onGatewayEspNowReceive);
}
```

- [ ] **Step 4: Dispatch `ServiceId::Trust` in `processEspNowDatagram`** — alongside the existing `ServiceId::Gateway` branch, add:

```cpp
if (decodeEnvelope(assembled.data(), assembled.size(), envelope, jsonBody, envError) &&
    envelope.serviceId == ServiceId::Trust) {
    JsonDocument document;
    if (!deserializeJson(document, jsonBody.data(), jsonBody.size())) {
        handleTrustFromEspNow(datagram.mac, document.as<JsonObjectConst>());
    }
    return;
}
```

(placed before the existing `ServiceId::Gateway` check, matching the same early-return shape.)

- [ ] **Step 5: Implement `handleTrustFromEspNow`, `sendTrustFrameTo`, `ensureUnencryptedPeer`, `upgradeToEncryptedPeer`, `beginPairing`, `confirmPairing`, `denyPairing`, `pairingStatus`** in `GatewayRelay.cpp` — the same shape as `EspNowEndpoint`'s equivalents (Task 6 Step 5), adapted to this class's members/helpers: `sendTrustFrameTo` mirrors `sendGatewayLinkFrame`'s envelope/fragment/send shape but unicast (parallel to how Task 6 generalized `sendEnvelope` into `sendEnvelopeTo`), and newly-paired records use `TrustRole::Client` since a gateway pairs with clients, not `TrustRole::Gateway`:

```cpp
void GatewayRelay::sendTrustFrameTo(const std::array<uint8_t, 6>& toMac, const char* name, JsonObject body) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = name;
    wrapper["body"].set(body);

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Event;
    envelope.serviceId = ServiceId::Trust;
    envelope.codecId = CodecId::Json;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Event;
    header.profileId = CarrierProfileId::EspNowV1;
    header.linkMessageId = gatewayLocalLinkMessageCounter_++;

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), header, kEspNowMaxDatagramBytes, frames,
                              fragmentError)) {
        return;
    }
    for (const auto& frame : frames) esp_now_send(toMac.data(), frame.data(), frame.size());
}

bool GatewayRelay::ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac) {
    if (esp_now_is_peer_exist(mac.data())) return true;
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool GatewayRelay::upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac, const std::array<uint8_t, 16>& lmk) {
    esp_now_del_peer(mac.data());
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, lmk.data(), lmk.size());
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool GatewayRelay::beginPairing(const std::array<uint8_t, 6>& targetMac) {
    if (trustPairing_.state() != esplink::TrustPairingState::Idle) return false;
    if (!ensureUnencryptedPeer(targetMac)) return false;

    esplink::TrustHelloMessage hello;
    if (!trustPairing_.beginAsInitiator(trustConfig_.identity(), millis(), hello)) return false;
    pairingTargetMac_ = targetMac;

    JsonDocument body;
    body["staticPubKey"] =
        bytesToBase64(std::vector<uint8_t>(hello.staticPublicKey.begin(), hello.staticPublicKey.end()));
    body["ephemeralPubKey"] =
        bytesToBase64(std::vector<uint8_t>(hello.ephemeralPublicKey.begin(), hello.ephemeralPublicKey.end()));
    body["nonce"] = bytesToBase64(std::vector<uint8_t>(hello.nonce.begin(), hello.nonce.end()));
    body["signature"] = bytesToBase64(std::vector<uint8_t>(hello.signature.begin(), hello.signature.end()));
    sendTrustFrameTo(targetMac, "trust.pair.begin", body.as<JsonObject>());
    return true;
}

void GatewayRelay::confirmPairing() {
    esplink::TrustSignature signature{};
    if (!trustPairing_.confirmLocally(millis(), signature)) return;

    JsonDocument body;
    body["confirmSignature"] = bytesToBase64(std::vector<uint8_t>(signature.begin(), signature.end()));
    sendTrustFrameTo(pairingTargetMac_, "trust.pair.confirm", body.as<JsonObject>());

    if (trustPairing_.state() == esplink::TrustPairingState::Committed) {
        esplink::TrustRecord record;
        record.staticPublicKey = trustPairing_.peerHello().staticPublicKey;
        record.mac = pairingTargetMac_;
        record.peerRole = esplink::TrustRole::Client;  // GatewayRelay always pairs with a client
        record.pairedAtMs = millis();
        std::string persistError;
        trustConfig_.addRecord(record, persistError);
        upgradeToEncryptedPeer(pairingTargetMac_, trustPairing_.derivedKeys().lmk);
        trustPairing_.reset();
    }
}

void GatewayRelay::denyPairing() {
    bool shouldSend = false;
    trustPairing_.cancel(shouldSend);
    if (shouldSend) sendTrustFrameTo(pairingTargetMac_, "trust.pair.cancel", JsonDocument().to<JsonObject>());
    esp_now_del_peer(pairingTargetMac_.data());
    trustPairing_.reset();
}

GatewayRelay::TrustPairingUiStatus GatewayRelay::pairingStatus() const {
    TrustPairingUiStatus status;
    esplink::TrustPairingOutcome outcome;
    switch (trustPairing_.state()) {
        case esplink::TrustPairingState::Idle:
            status.state = TrustPairingUiState::Idle;
            break;
        case esplink::TrustPairingState::AwaitingPeerHello:
            status.state = TrustPairingUiState::Discovering;
            break;
        case esplink::TrustPairingState::AwaitingApproval:
        case esplink::TrustPairingState::AwaitingPeerConfirm:
            status.state = TrustPairingUiState::AwaitingApproval;
            if (trustPairing_.currentOutcome(outcome)) {
                esplink::TrustHash hash{};
                trustCrypto_.sha256(outcome.peerStaticPublicKey.data(), outcome.peerStaticPublicKey.size(), hash);
                status.peerFingerprint = esplink::trustFingerprint(hash);
                status.numericCode = outcome.numericCode;
            }
            break;
        case esplink::TrustPairingState::Committed:
            status.state = TrustPairingUiState::Committed;
            break;
        case esplink::TrustPairingState::Cancelled:
            status.state = TrustPairingUiState::Cancelled;
            break;
    }
    return status;
}

std::vector<std::string> GatewayRelay::fingerprintList() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const esplink::TrustRecord* record = trustConfig_.store().at(i);
        esplink::TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        out.push_back(esplink::trustFingerprint(hash));
    }
    return out;
}

bool GatewayRelay::forgetByFingerprint(const std::string& fingerprint) {
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const esplink::TrustRecord* record = trustConfig_.store().at(i);
        esplink::TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        if (esplink::trustFingerprint(hash) == fingerprint) {
            std::string error;
            return trustConfig_.forgetRecord(record->staticPublicKey, error);
        }
    }
    return false;
}

void GatewayRelay::handleTrustFromEspNow(const std::array<uint8_t, 6>& fromMac, JsonObjectConst wrapper) {
    const char* name = wrapper["name"] | "";
    JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();

    if (std::strcmp(name, "trust.pair.begin") == 0) {
        std::vector<uint8_t> bytes;
        esplink::TrustHelloMessage peerHello;
        if (!bytesFromBase64(body["staticPubKey"] | "", bytes) || bytes.size() != esplink::kTrustPublicKeyBytes) {
            return;
        }
        std::memcpy(peerHello.staticPublicKey.data(), bytes.data(), bytes.size());
        if (!bytesFromBase64(body["ephemeralPubKey"] | "", bytes) || bytes.size() != esplink::kTrustPublicKeyBytes) {
            return;
        }
        std::memcpy(peerHello.ephemeralPublicKey.data(), bytes.data(), bytes.size());
        if (!bytesFromBase64(body["nonce"] | "", bytes) || bytes.size() != esplink::kTrustNonceBytes) return;
        std::memcpy(peerHello.nonce.data(), bytes.data(), bytes.size());
        if (!bytesFromBase64(body["signature"] | "", bytes) || bytes.size() != esplink::kTrustSignatureBytes) {
            return;
        }
        std::memcpy(peerHello.signature.data(), bytes.data(), bytes.size());

        const esplink::TrustRecord* existing = trustConfig_.store().findByMac(fromMac);
        if (existing != nullptr) {
            // Reconnect fast path -- see spec §1 "Reconnect"; no human approval needed.
            if (!esplink::verifyTrustHello(trustCrypto_, existing->staticPublicKey, peerHello)) return;
            esplink::TrustKeyPair ourEphemeral;
            esplink::TrustNonce ourNonce;
            esplink::TrustHelloMessage ourHello;
            if (!esplink::buildTrustHello(trustCrypto_, trustConfig_.identity(), ourEphemeral, ourNonce, ourHello)) {
                return;
            }
            esplink::TrustDerivedKeys derived;
            if (!esplink::deriveFromHellos(trustCrypto_, trustConfig_.identity(), ourEphemeral, ourNonce, peerHello,
                                          derived)) {
                return;
            }
            ensureUnencryptedPeer(fromMac);
            JsonDocument replyBody;
            replyBody["staticPubKey"] =
                bytesToBase64(std::vector<uint8_t>(ourHello.staticPublicKey.begin(), ourHello.staticPublicKey.end()));
            replyBody["ephemeralPubKey"] = bytesToBase64(
                std::vector<uint8_t>(ourHello.ephemeralPublicKey.begin(), ourHello.ephemeralPublicKey.end()));
            replyBody["nonce"] = bytesToBase64(std::vector<uint8_t>(ourHello.nonce.begin(), ourHello.nonce.end()));
            replyBody["signature"] =
                bytesToBase64(std::vector<uint8_t>(ourHello.signature.begin(), ourHello.signature.end()));
            sendTrustFrameTo(fromMac, "trust.pair.begin", replyBody.as<JsonObject>());
            upgradeToEncryptedPeer(fromMac, derived.lmk);
            return;
        }

        if (!ensureUnencryptedPeer(fromMac)) return;
        esplink::TrustHelloMessage reply;
        bool hasReply = false;
        if (!trustPairing_.onPeerHello(peerHello, trustConfig_.identity(), millis(), reply, hasReply)) return;
        pairingTargetMac_ = fromMac;
        if (hasReply) {
            JsonDocument replyBody;
            replyBody["staticPubKey"] =
                bytesToBase64(std::vector<uint8_t>(reply.staticPublicKey.begin(), reply.staticPublicKey.end()));
            replyBody["ephemeralPubKey"] = bytesToBase64(
                std::vector<uint8_t>(reply.ephemeralPublicKey.begin(), reply.ephemeralPublicKey.end()));
            replyBody["nonce"] = bytesToBase64(std::vector<uint8_t>(reply.nonce.begin(), reply.nonce.end()));
            replyBody["signature"] =
                bytesToBase64(std::vector<uint8_t>(reply.signature.begin(), reply.signature.end()));
            sendTrustFrameTo(fromMac, "trust.pair.begin", replyBody.as<JsonObject>());
        }
        return;
    }

    if (std::strcmp(name, "trust.pair.confirm") == 0) {
        std::vector<uint8_t> signatureBytes;
        if (!bytesFromBase64(body["confirmSignature"] | "", signatureBytes) ||
            signatureBytes.size() != esplink::kTrustSignatureBytes) {
            return;
        }
        esplink::TrustSignature signature{};
        std::memcpy(signature.data(), signatureBytes.data(), signature.size());
        if (!trustPairing_.onPeerConfirm(signature, millis())) return;
        if (trustPairing_.state() == esplink::TrustPairingState::Committed) {
            esplink::TrustRecord record;
            record.staticPublicKey = trustPairing_.peerHello().staticPublicKey;
            record.mac = fromMac;
            record.peerRole = esplink::TrustRole::Client;
            record.pairedAtMs = millis();
            std::string persistError;
            trustConfig_.addRecord(record, persistError);
            upgradeToEncryptedPeer(fromMac, trustPairing_.derivedKeys().lmk);
            trustPairing_.reset();
        }
        return;
    }

    if (std::strcmp(name, "trust.pair.cancel") == 0) {
        esp_now_del_peer(fromMac.data());
        trustPairing_.reset();
    }
}
```

(`bytesToBase64`/`bytesFromBase64` come from `EspBarcodeCore.h`, already included transitively the same way `EspNowEndpoint.cpp` uses them — add `#include "EspBarcodeCore.h"` to `GatewayRelay.cpp` if it is not already pulled in.)

- [ ] **Step 6: Enforce Secure Pairing in `processEspNowDatagram`** — before relaying to USB, drop untrusted traffic when enabled:

```cpp
if (trustConfig_.securePairingEnabled() && trustConfig_.store().findByMac(datagram.mac) == nullptr) {
    return;  // Secure Pairing on, this MAC isn't a trusted client: drop rather than relay
}
```

(placed after the `ServiceId::Trust`/`ServiceId::Gateway` early-returns, so pairing/discovery traffic is unaffected.)

- [ ] **Step 7: Route outbound relay traffic per-client instead of broadcasting** — change `relayToEspNow` so that when Secure Pairing is on, it sends to the specific trusted client's MAC (looked up by `HopFrameHeader.routeId`) instead of `kBroadcastAddress`:

```cpp
void GatewayRelay::relayToEspNow(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled) {
    const HopFrameHeader outHeader =
        relayHeaderFor(sourceHeader, CarrierProfileId::EspNowV1, usbToEspNowLinkMessageCounter_++);
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    if (!fragmentIntoHopFrames(assembled.data(), assembled.size(), outHeader, kEspNowMaxDatagramBytes, frames,
                               error)) {
        return;
    }

    const uint8_t* destination = kBroadcastAddress;
    if (trustConfig_.securePairingEnabled() && outHeader.routeId != 0) {
        for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
            const esplink::TrustRecord* record = trustConfig_.store().at(i);
            if (record->routeId == outHeader.routeId) {
                destination = record->mac.data();
                break;
            }
        }
    }
    for (const auto& frame : frames) esp_now_send(destination, frame.data(), frame.size());
}
```

(the USB host is responsible for stamping `routeId` on outbound frames once it knows which paired client it's targeting — Task 10/11 surface the trust list with each client's `routeId` so the .NET client can do this; until a host sends a non-zero `routeId`, behavior is unchanged from today's broadcast-everything.)

- [ ] **Step 8: Add `trust.controllers.list`/`trust.controller.forget`/`trust.pair.begin`/`trust.pair.cancel`/`trust.pair.status` to a new `handleTrustFromUsb` method**, called from `processUsbCobsBlock` the same way `handleGatewayServiceFromUsb` is (check `envelope.serviceId == ServiceId::Trust` right after/alongside the existing `ServiceId::Gateway` check). Field names follow this file's existing snake_case wire convention (`last_seen_ms_ago`, `via_relay`, etc., as seen in `gateway.peers.list`'s response), and `trust.pair.status` is what the .NET/Blazor UI polls to show the fingerprint/code while a pairing is pending (Task 9/10) — `beginPairing`/`confirmPairing`/`denyPairing`/`pairingStatus` don't otherwise reach the USB host, only the ESP-NOW peer:

```cpp
namespace {
// Duplicated locally rather than shared with TrustConfigStore.cpp -- this file already
// duplicates formatMac() the same way rather than sharing it with EspNowEndpoint.cpp.
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

bool GatewayRelay::handleTrustFromUsb(const HopFrameHeader& sourceHeader, const MessageEnvelope& envelope,
                                      JsonObjectConst wrapper) {
    const char* name = wrapper["name"] | "";
    JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();

    JsonDocument responseWrapper;
    responseWrapper["schema"] = "esbg.control/2.0";
    responseWrapper["name"] = name;

    if (std::strcmp(name, "trust.controllers.list") == 0) {
        JsonArray peers = responseWrapper["body"]["peers"].to<JsonArray>();
        for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
            const esplink::TrustRecord* record = trustConfig_.store().at(i);
            JsonObject entry = peers.add<JsonObject>();
            esplink::TrustHash hash{};
            trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
            entry["fingerprint"] = esplink::trustFingerprint(hash);
            entry["mac"] = formatMac(record->mac);
            entry["route_id"] = record->routeId;
            entry["paired_at_ms"] = record->pairedAtMs;
            entry["label"] = record->label;
        }
    } else if (std::strcmp(name, "trust.controller.forget") == 0) {
        const bool found = forgetByFingerprint(body["fingerprint"] | "");
        responseWrapper["body"]["ok"] = found;
        if (!found) {
            responseWrapper["error"]["code"] = "not_found";
            responseWrapper["error"]["message"] = "no trust record with that fingerprint";
        }
    } else if (std::strcmp(name, "trust.pair.begin") == 0) {
        std::array<uint8_t, 6> targetMac{};
        macFromString(body["mac"] | "", targetMac);
        responseWrapper["body"]["ok"] = beginPairing(targetMac);
    } else if (std::strcmp(name, "trust.pair.cancel") == 0) {
        denyPairing();
        responseWrapper["body"]["ok"] = true;
    } else if (std::strcmp(name, "trust.pair.status") == 0) {
        const TrustPairingUiStatus status = pairingStatus();
        JsonObject responseBody = responseWrapper["body"].to<JsonObject>();
        switch (status.state) {
            case TrustPairingUiState::Idle: responseBody["state"] = "idle"; break;
            case TrustPairingUiState::Discovering: responseBody["state"] = "discovering"; break;
            case TrustPairingUiState::AwaitingApproval: responseBody["state"] = "awaiting_approval"; break;
            case TrustPairingUiState::Committed: responseBody["state"] = "committed"; break;
            case TrustPairingUiState::Cancelled: responseBody["state"] = "cancelled"; break;
        }
        if (!status.peerFingerprint.empty()) responseBody["fingerprint"] = status.peerFingerprint;
        if (status.numericCode != 0) responseBody["numeric_code"] = status.numericCode;
    } else {
        responseWrapper["error"]["code"] = "unknown_command";
        responseWrapper["error"]["message"] = "trust command not supported this release";
    }

    std::string serialized;
    serializeJson(responseWrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendUsbGatewayResponse(sourceHeader, envelope, bodyBytes);
    return true;
}
```

- [ ] **Step 9: Build and verify it compiles**

Run: `pio run -e esp32dev`
Expected: build succeeds.

- [ ] **Step 10: Manual hardware validation** — with Secure Pairing on: pair a client, confirm `trust.controllers.list` (via the Blazor Trust card once Task 10 exists, or a raw USB serial probe in the meantime) shows one record with `route_id=1`; confirm relay traffic for that client is now unicast (a packet sniffer or a second unpaired board no longer sees it); forget the client and confirm it can no longer relay until re-paired.

- [ ] **Step 11: Commit**

```bash
git add src/GatewayRelay.h src/GatewayRelay.cpp
git commit -m "feat(trust): enforce pairing, add trust.* USB commands, and route by client to GatewayRelay"
```

---

## Task 8: On-device UI (Settings toggle + Trust screen)

**Files:**
- Modify: `include/BarcodeApplication.h`
- Modify: `src/BarcodeApplication.cpp`

**Interfaces:**
- Consumes: `EspNowEndpoint::beginPairing/confirmPairing/denyPairing/pairingStatus/fingerprintList/forgetByFingerprint` (Task 6), `GatewayRelay`'s identical set (Task 7).
- Produces: `View::Trust`, a Settings row toggling Secure Pairing, wired the same way `View::Gateway` already is.

- [ ] **Step 1: Add `View::Trust` and new members to `include/BarcodeApplication.h`**

```cpp
// enum class View gains Trust:
enum class View : uint8_t { Home, TypePicker, Options, Presets, Settings, Barcode, Gateway, Trust };

// New public methods (alongside enterGatewayMode()):
void updateTrustPairingStatus(bool discovering, const std::string& peerFingerprint, uint32_t numericCode,
                              bool committed, bool cancelled);
void updateGatewayRelayTrustedPeers(const std::vector<std::string>& fingerprints);
void updateEspNowTrustedPeers(const std::vector<std::string>& fingerprints);
bool consumeTrustPairRequest(std::array<uint8_t, 6>& outTargetMac);  // "Pair new device" tapped
bool consumeTrustConfirmRequest();
bool consumeTrustDenyRequest();
bool consumeTrustForgetRequest(std::string& outFingerprint);
// Tapped the Secure Pairing switch in Settings -- flips the locally-cached display value
// immediately (so the switch redraws right away) and sets outValue for main.cpp to push into
// whichever of EspNowEndpoint's/GatewayRelay's TrustConfigStore is actually active (Step 6);
// BarcodeApplication has no TrustConfigStore of its own, only this cached display flag.
bool consumeSecurePairingToggleRequest(bool& outValue);
void refreshSecurePairingEnabled(bool value) { securePairingEnabled_ = value; }  // reflects a push from main.cpp
bool securePairingEnabled() const { return securePairingEnabled_; }

// New private methods (alongside handleGatewayTouch):
void drawTrust();
void handleTrustTouch(uint16_t x, uint16_t y);

// New private state (alongside gatewayPingRequested_):
bool securePairingEnabled_ = false;
bool trustPairRequested_ = false;
std::array<uint8_t, 6> trustPairTargetMac_{};
bool trustConfirmRequested_ = false;
bool trustDenyRequested_ = false;
bool trustForgetRequested_ = false;
std::string trustForgetFingerprint_;
bool trustDiscovering_ = false;
std::string trustPeerFingerprint_;
uint32_t trustNumericCode_ = 0;
bool trustCommitted_ = false;
bool trustCancelled_ = false;
```

- [ ] **Step 2: Wire the new `View` into the existing dispatch switches** — add `case View::Trust: drawTrust(); break;` next to line 544's `case View::Gateway: drawGateway(); break;`, and `case View::Trust: handleTrustTouch(x, y); break;` next to line 627's `case View::Gateway: handleGatewayTouch(x, y); break;`.

- [ ] **Step 3: Add a "Secure Pairing" row to `drawSettings()`/`handleSettingsTouch()`** — extend the existing 2-row `rows` array (barcode/editor orientation, lines 1381–1384) to a 3rd row showing "Secure Pairing: On/Off" with a mini switch (reusing `drawMiniSwitch`, already used for the theme toggle), and extend `handleSettingsTouch`'s row-index branch (lines 1433–1441) so tapping that row sets `securePairingToggleRequested_ = true; securePairingToggleValue_ = !securePairingEnabled_;` and redraws immediately using that new (not-yet-confirmed-persisted) value. Add a "Trust" navigation button below the card (same shape as `drawGatewayHomeBanner`'s button-to-`View::Gateway` pattern) that switches to `View::Trust`.

Add the two new private members this needs, alongside `securePairingEnabled_`:

```cpp
bool securePairingToggleRequested_ = false;
bool securePairingToggleValue_ = false;
```

And implement `consumeSecurePairingToggleRequest`:

```cpp
bool BarcodeApplication::consumeSecurePairingToggleRequest(bool& outValue) {
    if (!securePairingToggleRequested_) return false;
    securePairingToggleRequested_ = false;
    outValue = securePairingToggleValue_;
    securePairingEnabled_ = securePairingToggleValue_;  // optimistic UI update; Step 6 corrects it if persistence fails
    return true;
}
```

- [ ] **Step 4: Implement `drawTrust()`** — following `drawGateway()`'s layout conventions (sub-header, cards, list with a "no X yet" empty state): a "Pair new device" button; while `trustDiscovering_`/awaiting-approval, an overlay card with `trustPeerFingerprint_` + `trustNumericCode_` (large, `setTextSize`/font 4 or similar, matching how `drawGateway()`'s stat tiles use font 2) + Confirm/Deny buttons; otherwise, a scrollable list of trusted peers (reusing the same row-drawing shape as `drawGateway()`'s peer list, lines 1496–1520) each with a "Forget" tap target.

```cpp
void BarcodeApplication::drawTrust() {
    applyOrientationForView(View::Trust);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader("Trust");

    if (trustDiscovering_ || !trustPeerFingerprint_.empty()) {
        const Rect card = settingsCardRect(width, height, contentTop);  // reuse the existing card geometry helper
        tft_.fillRoundRect(card.x, card.y, card.w, card.h, 12, th.surface);
        tft_.drawRoundRect(card.x, card.y, card.w, card.h, 12, th.hairline);
        tft_.setTextDatum(TC_DATUM);
        tft_.setTextColor(th.text, th.surface);
        const std::string title = trustDiscovering_ ? "Waiting for peer..." : ("Pair with " + trustPeerFingerprint_ + "?");
        tft_.drawString(title.c_str(), static_cast<int16_t>(card.x + card.w / 2), static_cast<int16_t>(card.y + 16), 2);
        if (!trustDiscovering_) {
            char code[8];
            std::snprintf(code, sizeof(code), "%06u", trustNumericCode_);
            tft_.setTextDatum(MC_DATUM);
            tft_.drawString(code, static_cast<int16_t>(card.x + card.w / 2), static_cast<int16_t>(card.y + card.h / 2), 4);

            const Rect confirmBtn{static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + card.h - 44),
                                 static_cast<int16_t>(card.w / 2 - 18), 32};
            const Rect denyBtn{static_cast<int16_t>(card.x + card.w / 2 + 6), static_cast<int16_t>(card.y + card.h - 44),
                              static_cast<int16_t>(card.w / 2 - 18), 32};
            tft_.fillRoundRect(confirmBtn.x, confirmBtn.y, confirmBtn.w, confirmBtn.h, 8, th.accent);
            tft_.setTextDatum(MC_DATUM);
            tft_.setTextColor(th.accentText, th.accent);
            tft_.drawString("CONFIRM", static_cast<int16_t>(confirmBtn.x + confirmBtn.w / 2),
                            static_cast<int16_t>(confirmBtn.y + confirmBtn.h / 2), 1);
            tft_.fillRoundRect(denyBtn.x, denyBtn.y, denyBtn.w, denyBtn.h, 8, th.danger);
            tft_.setTextColor(TFT_WHITE, th.danger);
            tft_.drawString("DENY", static_cast<int16_t>(denyBtn.x + denyBtn.w / 2),
                            static_cast<int16_t>(denyBtn.y + denyBtn.h / 2), 1);
        }
        return;
    }

    const Rect pairButton = gatewayPingButtonRect(contentTop, width);  // reuse an existing button-rect helper's shape
    tft_.fillRoundRect(pairButton.x, pairButton.y, pairButton.w, pairButton.h,
                       static_cast<int16_t>(pairButton.h / 2), th.accent);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(th.accentText, th.accent);
    tft_.drawString("PAIR NEW DEVICE", static_cast<int16_t>(pairButton.x + pairButton.w / 2),
                    static_cast<int16_t>(pairButton.y + pairButton.h / 2), 1);

    const int16_t listTop = static_cast<int16_t>(pairButton.y + pairButton.h + 8);
    const Rect listCard = gatewayPeersCardRect(width, height, listTop);
    tft_.fillRoundRect(listCard.x, listCard.y, listCard.w, listCard.h, 10, th.surface);
    tft_.drawRoundRect(listCard.x, listCard.y, listCard.w, listCard.h, 10, th.hairline);
    // Row rendering mirrors drawGateway()'s peer list (lines 1496-1520): one row per trusted
    // peer with its fingerprint/label on the left and a "Forget" tap target on the right, or a
    // "No paired devices yet" faint line if there are none. gatewayModeActive_ (already used
    // elsewhere in this file to distinguish gateway-vs-client behavior) selects the source:
    // gatewayModeActive_ ? gatewayRelayTrustedPeers_ : espNowTrustedPeers_ (see Step 6 below for
    // how these two BarcodeApplication-owned snapshots get populated each loop() tick, the same
    // way gatewayStats_/gatewayLinkStatus_ already do for the Gateway screen).
    const std::vector<TrustPeerRow>& peers = gatewayModeActive_ ? gatewayRelayTrustedPeers_ : espNowTrustedPeers_;
    const int16_t peerRowH = 16;
    std::size_t shown = 0;
    int16_t y = static_cast<int16_t>(listCard.y + 4);
    for (; shown < peers.size(); ++shown) {
        if (y + peerRowH > listCard.y + listCard.h - 4) break;
        const TrustPeerRow& peer = peers[shown];
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.text, th.surface);
        tft_.drawString(peer.fingerprint.c_str(), static_cast<int16_t>(listCard.x + 8), static_cast<int16_t>(y + 2), 1);
        const Rect forgetBtn{static_cast<int16_t>(listCard.x + listCard.w - 56), y, 48, peerRowH};
        tft_.fillRoundRect(forgetBtn.x, forgetBtn.y, forgetBtn.w, forgetBtn.h, 4, th.danger);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(TFT_WHITE, th.danger);
        tft_.drawString("X", static_cast<int16_t>(forgetBtn.x + forgetBtn.w / 2),
                        static_cast<int16_t>(forgetBtn.y + forgetBtn.h / 2), 1);
        y = static_cast<int16_t>(y + peerRowH);
    }
    if (peers.empty()) {
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.textFaint, th.surface);
        tft_.drawString("No paired devices yet", static_cast<int16_t>(listCard.x + 8),
                        static_cast<int16_t>(listCard.y + 8), 1);
    }
}
```

Add the small `TrustPeerRow` value type and the two per-mode snapshot members next to `gatewayStats_`/`gatewayLinkStatus_` in `include/BarcodeApplication.h`:

```cpp
struct TrustPeerRow {
    std::string fingerprint;
    Rect forgetButtonRect;  // last-drawn hit-test rect for this row, filled by drawTrust()
};
std::vector<TrustPeerRow> gatewayRelayTrustedPeers_;
std::vector<TrustPeerRow> espNowTrustedPeers_;
```

(`handleTrustTouch`'s per-row Forget tap test reuses each row's stored `forgetButtonRect`, the same way `drawTrust()` computed `forgetBtn` above — store it into `TrustPeerRow::forgetButtonRect` as each row is drawn rather than recomputing row geometry in the touch handler.)

(the discovered-peers picker for "Pair new device" reuses the existing `gatewayLinkStatus_`/`gatewayStats_.linkStats.peers` data Task 6/7 already populate — tapping "PAIR NEW DEVICE" when not gateway-mode picks the single currently-discovered gateway MAC directly, since `EspNowEndpoint` is inherently 1:1; in gateway mode it shows the peer list from `gatewayStats_.linkStats.peers` filtered to MACs not already in `trustedPeers()`, same list `drawGateway()` already renders, and tapping a row sets `trustPairTargetMac_`/`trustPairRequested_`.)

- [ ] **Step 5: Implement `handleTrustTouch()`** mirroring `handleGatewayTouch`'s shape: back-chevron, pair button, confirm/deny buttons when awaiting approval, per-row forget taps when listing trusted peers.

- [ ] **Step 6: Wire `main.cpp`'s `loop()`** to shuttle requests between `BarcodeApplication` and whichever of `EspNowEndpoint`/`GatewayRelay` is active — mirroring the existing `consumeGatewayPingRequest()`/`gatewayRelay.pingNow()` pattern (lines 92–94, 99–100):

```cpp
// Alongside the existing gatewayRelay.pingNow()/updateGatewayStats() calls in the
// GatewayRelayMode branch:
std::array<uint8_t, 6> pairTarget{};
if (application.consumeTrustPairRequest(pairTarget)) gatewayRelay.beginPairing(pairTarget);
if (application.consumeTrustConfirmRequest()) gatewayRelay.confirmPairing();
if (application.consumeTrustDenyRequest()) gatewayRelay.denyPairing();
std::string forgetFingerprint;
if (application.consumeTrustForgetRequest(forgetFingerprint)) gatewayRelay.forgetByFingerprint(forgetFingerprint);
const auto gwStatus = gatewayRelay.pairingStatus();
application.updateTrustPairingStatus(gwStatus.state == esplink::GatewayRelay::TrustPairingUiState::Discovering,
                                     gwStatus.peerFingerprint, gwStatus.numericCode,
                                     gwStatus.state == esplink::GatewayRelay::TrustPairingUiState::Committed,
                                     gwStatus.state == esplink::GatewayRelay::TrustPairingUiState::Cancelled);
application.updateGatewayRelayTrustedPeers(gatewayRelay.fingerprintList());
bool toggleValue = false;
if (application.consumeSecurePairingToggleRequest(toggleValue)) {
    std::string toggleError;
    if (!gatewayRelay.setSecurePairingEnabled(toggleValue, toggleError)) {
        application.refreshSecurePairingEnabled(gatewayRelay.securePairingEnabled());  // persistence failed: revert display
    }
}

// Alongside the existing espNowEndpoint.loop()/updateGatewayLinkStatus() calls in the non-gateway
// branch, the mirror-image four calls targeting espNowEndpoint instead of gatewayRelay:
std::array<uint8_t, 6> directPairTarget{};
if (application.consumeTrustPairRequest(directPairTarget)) espNowEndpoint.beginPairing(directPairTarget);
if (application.consumeTrustConfirmRequest()) espNowEndpoint.confirmPairing();
if (application.consumeTrustDenyRequest()) espNowEndpoint.denyPairing();
std::string directForgetFingerprint;
if (application.consumeTrustForgetRequest(directForgetFingerprint)) {
    espNowEndpoint.forgetByFingerprint(directForgetFingerprint);
}
const auto directStatus = espNowEndpoint.pairingStatus();
application.updateTrustPairingStatus(directStatus.state == esplink::EspNowEndpoint::TrustPairingUiState::Discovering,
                                     directStatus.peerFingerprint, directStatus.numericCode,
                                     directStatus.state == esplink::EspNowEndpoint::TrustPairingUiState::Committed,
                                     directStatus.state == esplink::EspNowEndpoint::TrustPairingUiState::Cancelled);
application.updateEspNowTrustedPeers(espNowEndpoint.fingerprintList());
bool directToggleValue = false;
if (application.consumeSecurePairingToggleRequest(directToggleValue)) {
    std::string toggleError;
    if (!espNowEndpoint.setSecurePairingEnabled(directToggleValue, toggleError)) {
        application.refreshSecurePairingEnabled(espNowEndpoint.securePairingEnabled());
    }
}
```

Add the two new `BarcodeApplication` methods these calls need (alongside `updateGatewayStats`/`updateGatewayLinkStatus`), each converting a fingerprint list into the `TrustPeerRow` list `drawTrust()` (Step 4) reads:

```cpp
void BarcodeApplication::updateGatewayRelayTrustedPeers(const std::vector<std::string>& fingerprints) {
    gatewayRelayTrustedPeers_.clear();
    for (const auto& fingerprint : fingerprints) gatewayRelayTrustedPeers_.push_back(TrustPeerRow{fingerprint, {}});
}

void BarcodeApplication::updateEspNowTrustedPeers(const std::vector<std::string>& fingerprints) {
    espNowTrustedPeers_.clear();
    for (const auto& fingerprint : fingerprints) espNowTrustedPeers_.push_back(TrustPeerRow{fingerprint, {}});
}
```

(both take `fingerprintList()`'s output — see the `EspNowEndpoint::fingerprintList()`/`GatewayRelay::fingerprintList()` accessors added in Task 6/7, which already do the `sha256`+`trustFingerprint` loop internally where `trustCrypto_` is in scope; `BarcodeApplication` doesn't need its own `ITrustCrypto` at all.)

- [ ] **Step 7: Build the firmware and verify it compiles**

Run: `pio run -e esp32dev`
Expected: build succeeds.

- [ ] **Step 8: Manual hardware validation** — flash two boards (one gateway, one client, or two direct-mode boards); from Settings, toggle Secure Pairing on both; open Trust, tap "Pair new device", confirm the same 6-digit code appears on both screens, tap Confirm on both, confirm the peer now appears in each Trust list; tap Forget on one and confirm it drops out of the trusted list and stops working until re-paired. This is the checklist item from the spec's §6 that could not be automated.

- [ ] **Step 9: Commit**

```bash
git add include/BarcodeApplication.h src/BarcodeApplication.cpp src/main.cpp
git commit -m "feat(trust): add on-device Secure Pairing toggle and Trust screen"
```

---

## Task 9: `.NET` `GatewayLinkClient` trust management

**Files:**
- Modify: `dotnet/src/EspBarcode.Controller.Web/Services/GatewayLinkClient.cs`
- Modify: `dotnet/src/EspBarcode.Controller.Web/Models/BarcodeModels.cs`

**Interfaces:**
- Consumes: the firmware's `trust.controllers.list`/`trust.controller.forget`/`trust.pair.begin`/`trust.pair.cancel`/`trust.pair.status` JSON commands (Task 7 Step 8), over `ServiceId.Trust` (already defined as `Trust = 6` in `dotnet/src/EspBarcode.Protocol/ConnectivityEnums.cs`), via `GatewayLinkClient`'s existing private `SendAsync(ServiceId, string, JsonObject?, uint, TimeSpan, CancellationToken)` helper (the one `ListPeersAsync`/`PingNowAsync` already call).
- Produces: `GatewayLinkClient.ListTrustedPeersAsync/BeginPairingAsync/CancelPairingAsync/ForgetPeerAsync/PairingStatusAsync`, plus `TrustedPeer` and `TrustPairingStatus` models in `BarcodeModels.cs`. Task 10 (Blazor UI) consumes these.

- [ ] **Step 1: Add `TrustedPeer` and `TrustPairingStatus` to `BarcodeModels.cs`**, next to `GatewayPeer` (line 97):

```csharp
/// <summary>One paired ESP-NOW peer this gateway trusts — see <c>GatewayLinkClient.ListTrustedPeersAsync</c>.</summary>
public sealed record TrustedPeer(string Fingerprint, string Mac, int RouteId, long PairedAtMs, string Label);

/// <summary>The gateway's current pairing-attempt state — see <c>GatewayLinkClient.PairingStatusAsync</c>.</summary>
public sealed record TrustPairingStatus(string State, string? Fingerprint, int? NumericCode)
{
    public bool AwaitingApproval => State == "awaiting_approval";
    public bool InProgress => State is "discovering" or "awaiting_approval";
}
```

- [ ] **Step 2: Add the trust methods to `GatewayLinkClient`**, immediately after `PingNowAsync` (line 126), matching `ListPeersAsync`'s exact `SendAsync(ServiceId.Gateway, ...)` shape but with `ServiceId.Trust`:

```csharp
/// <summary>The gateway's own trust list — <c>trust.controllers.list</c>, answered locally (Task 7 Step 8).</summary>
public async Task<IReadOnlyList<TrustedPeer>> ListTrustedPeersAsync(
    TimeSpan? timeout = null, CancellationToken cancellationToken = default)
{
    var body = await SendAsync(
        ServiceId.Trust, "trust.controllers.list", null, controlSessionId: 0,
        timeout ?? TimeSpan.FromSeconds(5), cancellationToken);
    var peers = body["peers"]?.AsArray() ?? [];
    return peers.Select(p =>
    {
        var o = p!.AsObject();
        return new TrustedPeer(
            o["fingerprint"]!.GetValue<string>(), o["mac"]!.GetValue<string>(),
            o["route_id"]!.GetValue<int>(), o["paired_at_ms"]!.GetValue<long>(),
            o["label"]?.GetValue<string>() ?? "");
    }).ToArray();
}

/// <summary>Arms this gateway's pairing window against a specific discovered MAC — <c>trust.pair.begin</c>.</summary>
public Task BeginPairingAsync(string mac, TimeSpan? timeout = null, CancellationToken cancellationToken = default)
    => SendAsync(ServiceId.Trust, "trust.pair.begin", new JsonObject { ["mac"] = mac }, controlSessionId: 0,
        timeout ?? TimeSpan.FromSeconds(5), cancellationToken);

/// <summary>Cancels an in-progress pairing attempt — <c>trust.pair.cancel</c>.</summary>
public Task CancelPairingAsync(TimeSpan? timeout = null, CancellationToken cancellationToken = default)
    => SendAsync(ServiceId.Trust, "trust.pair.cancel", null, controlSessionId: 0,
        timeout ?? TimeSpan.FromSeconds(5), cancellationToken);

/// <summary>Polls the current pairing-attempt state — <c>trust.pair.status</c>.</summary>
public async Task<TrustPairingStatus> PairingStatusAsync(
    TimeSpan? timeout = null, CancellationToken cancellationToken = default)
{
    var body = await SendAsync(
        ServiceId.Trust, "trust.pair.status", null, controlSessionId: 0,
        timeout ?? TimeSpan.FromSeconds(5), cancellationToken);
    return new TrustPairingStatus(
        body["state"]!.GetValue<string>(),
        body["fingerprint"]?.GetValue<string>(),
        body["numeric_code"]?.GetValue<int>());
}

/// <summary>Forgets a trusted peer by fingerprint — <c>trust.controller.forget</c>.</summary>
public async Task<bool> ForgetPeerAsync(
    string fingerprint, TimeSpan? timeout = null, CancellationToken cancellationToken = default)
{
    var body = await SendAsync(
        ServiceId.Trust, "trust.controller.forget", new JsonObject { ["fingerprint"] = fingerprint },
        controlSessionId: 0, timeout ?? TimeSpan.FromSeconds(5), cancellationToken);
    return body["ok"]?.GetValue<bool>() ?? false;
}
```

- [ ] **Step 3: Build the .NET solution and verify it compiles**

Run: `dotnet build dotnet/EspBarcode.sln`
Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Services/GatewayLinkClient.cs \
        dotnet/src/EspBarcode.Controller.Web/Models/BarcodeModels.cs
git commit -m "feat(trust): add trust management methods to GatewayLinkClient"
```

---

## Task 10: Blazor Trust UI

**Files:**
- Modify: `dotnet/src/EspBarcode.Controller.Web/Pages/Gateway.razor`

**Interfaces:**
- Consumes: `GatewayLinkClient.ListTrustedPeersAsync/BeginPairingAsync/CancelPairingAsync/ForgetPeerAsync/PairingStatusAsync`, `TrustedPeer`, `TrustPairingStatus` (Task 9).
- Produces: a Trust card per gateway device on the existing Gateway page, following this file's existing per-device-dictionary/`_busy`/`_errors`/`data-testid` conventions exactly (see `_peers`/`RefreshPeersAsync`/`PingNowAsync` for the pattern this mirrors).

- [ ] **Step 1: Add a Trust card to the markup**, right after the existing "Discovered / relayed peers" card (after line 65, before the barcode-generation form at line 67):

```razor
            <div class="esb-card" style="margin:0 0 12px; padding:10px;" data-testid="gateway-trust">
                <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:6px;">
                    <span class="esb-muted" style="font-weight:600;">Trusted devices</span>
                    <button class="esb-btn esb-btn-sm" type="button" disabled="@_busy" data-testid="gateway-pair-new-device"
                            @onclick="@(() => BeginPairingAsync(device))">
                        Pair new device
                    </button>
                </div>

                @if (_pairingStatus.TryGetValue(device.Id, out var pairing) && pairing.InProgress)
                {
                    <div data-testid="gateway-pairing-status" style="padding:6px 0; font-size:0.85em;">
                        @if (pairing.AwaitingApproval && !string.IsNullOrEmpty(pairing.Fingerprint))
                        {
                            <p style="margin:0;">
                                Peer <strong>@pairing.Fingerprint</strong> — code
                                <strong data-testid="gateway-pairing-code">@pairing.NumericCode?.ToString("D6")</strong>
                            </p>
                            <p class="esb-faint" style="margin:0;">Confirm this matches the code on both device screens, then tap Confirm on both.</p>
                        }
                        else
                        {
                            <p class="esb-faint" style="margin:0;">Waiting for the peer...</p>
                        }
                        <button class="esb-btn esb-btn-sm" type="button" data-testid="gateway-cancel-pairing"
                                @onclick="@(() => CancelPairingAsync(device))">
                            Cancel
                        </button>
                    </div>
                }

                @if (!_trustedPeers.TryGetValue(device.Id, out var trusted) || trusted.Count == 0)
                {
                    <p class="esb-faint" style="margin:0;" data-testid="gateway-trust-empty">No paired devices yet.</p>
                }
                else
                {
                    @foreach (var peer in trusted)
                    {
                        <div style="display:flex; justify-content:space-between; gap:8px; padding:3px 0; font-size:0.85em;" data-testid="gateway-trust-row">
                            <span>@peer.Fingerprint</span>
                            <span class="esb-faint">@peer.Mac (route @peer.RouteId)</span>
                            <button class="esb-btn esb-btn-sm" type="button" disabled="@_busy" data-testid="gateway-trust-forget"
                                    @onclick="@(() => ForgetPeerAsync(device, peer.Fingerprint))">
                                Forget
                            </button>
                        </div>
                    }
                }
            </div>
```

- [ ] **Step 2: Add the backing state and methods to `@code`**, alongside `_peers`/`RefreshPeersAsync` (after line 109/160):

```csharp
    private readonly Dictionary<string, IReadOnlyList<TrustedPeer>> _trustedPeers = new();
    private readonly Dictionary<string, TrustPairingStatus> _pairingStatus = new();

    private async Task RefreshTrustedPeersAsync(DeviceConnection device)
    {
        if (device.GatewayLink is null) return;
        _trustedPeers[device.Id] = await device.GatewayLink.ListTrustedPeersAsync();
    }

    private async Task BeginPairingAsync(DeviceConnection device)
    {
        if (device.GatewayLink is null) return;
        _busy = true;
        _errors.Remove(device.Id);
        try
        {
            // Reuses the MAC of the first discovered-but-not-yet-trusted peer from the existing
            // peers list (_peers, populated by RefreshPeersAsync/PingNowAsync above) rather than
            // introducing a second discovery source.
            var alreadyTrusted = (_trustedPeers.TryGetValue(device.Id, out var trusted) ? trusted : [])
                .Select(p => p.Mac).ToHashSet();
            var candidate = (_peers.TryGetValue(device.Id, out var discovered) ? discovered : [])
                .FirstOrDefault(p => !alreadyTrusted.Contains(p.Mac));
            if (candidate is null)
            {
                _errors[device.Id] = "No undiscovered peer to pair -- click \"Ping for Clients\" first.";
                return;
            }
            await device.GatewayLink.BeginPairingAsync(candidate.Mac);
            _pairingStatus[device.Id] = new TrustPairingStatus("discovering", null, null);
        }
        catch (Exception ex)
        {
            _errors[device.Id] = ex.Message;
        }
        finally
        {
            _busy = false;
        }
    }

    private async Task CancelPairingAsync(DeviceConnection device)
    {
        if (device.GatewayLink is null) return;
        try
        {
            await device.GatewayLink.CancelPairingAsync();
        }
        finally
        {
            _pairingStatus.Remove(device.Id);
        }
    }

    private async Task ForgetPeerAsync(DeviceConnection device, string fingerprint)
    {
        if (device.GatewayLink is null) return;
        _busy = true;
        try
        {
            await device.GatewayLink.ForgetPeerAsync(fingerprint);
            await RefreshTrustedPeersAsync(device);
        }
        catch (Exception ex)
        {
            _errors[device.Id] = ex.Message;
        }
        finally
        {
            _busy = false;
        }
    }
```

(unlike `RefreshPeersAsync`, which the operator triggers manually with a button, a pending pairing needs to keep polling `trust.pair.status` on its own so the fingerprint/code appear without an extra click — this file has no existing timer-based mechanism to mirror, so Step 3 adds one, scoped narrowly to only run while a pairing is actually in progress.)

- [ ] **Step 3: Add a small poll timer for in-progress pairings**, alongside the existing `OnInitialized`/`Dispose` (lines 114/209):

```csharp
    private System.Threading.Timer? _pairingPollTimer;

    protected override void OnInitialized()
    {
        Devices.Changed += OnChanged;
        _pairingPollTimer = new System.Threading.Timer(_ => _ = PollPairingStatusAsync(), null,
            TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1));
    }

    private async Task PollPairingStatusAsync()
    {
        foreach (var device in GatewayDevices.Where(d => _pairingStatus.ContainsKey(d.Id)))
        {
            if (device.GatewayLink is null) continue;
            try
            {
                var status = await device.GatewayLink.PairingStatusAsync();
                _pairingStatus[device.Id] = status;
                if (status.State is "committed" or "cancelled")
                {
                    _pairingStatus.Remove(device.Id);
                    if (status.State == "committed") await RefreshTrustedPeersAsync(device);
                }
            }
            catch
            {
                // Transient poll failure -- next tick tries again; don't surface as a page error.
            }
        }
        await InvokeAsync(StateHasChanged);
    }

    public void Dispose()
    {
        Devices.Changed -= OnChanged;
        _pairingPollTimer?.Dispose();
    }
```

(replaces the existing bare `protected override void OnInitialized() => Devices.Changed += OnChanged;` at line 114 and the existing `public void Dispose() => Devices.Changed -= OnChanged;` at line 209 with these expanded versions.)

- [ ] **Step 4: Build and run the Blazor app, verify the Trust card renders**

Run: the existing dev-server workflow already used for this app (per `dotnet/README.md`)
Expected: putting a fake/real board into gateway mode and opening the Gateway page shows a "Trusted devices" card with "No paired devices yet." and a working "Pair new device" button.

- [ ] **Step 5: Commit**

```bash
git add dotnet/src/EspBarcode.Controller.Web/Pages/Gateway.razor
git commit -m "feat(trust): add Trust card to the Blazor Gateway page"
```

---

## Task 11: E2E coverage for the Trust UI

**Files:**
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Gateway.feature`
- Modify: `dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GatewayStepDefinitions.cs`
- Modify: `dotnet/src/EspBarcode.Controller.Web/wwwroot/js/fakeSerial.js`

**Interfaces:**
- Consumes: the Trust card markup and its `data-testid` attributes (Task 10).
- Produces: new Gherkin scenarios + step definitions + fake-serial trust command responses.

- [ ] **Step 1: Add trust-command handling to `fakeSerial.js`**, alongside the existing `envelope.serviceId === 7` branch (line 375):

```javascript
            } else if (envelope.serviceId === 6) {
                // ServiceId::Trust -- answered locally, mirroring GatewayRelay's
                // handleTrustFromUsb (Task 7 Step 8).
                this._trustedPeers = this._trustedPeers ?? (config?.trustedPeers ?? []).map((p) => ({ ...p }));
                this._pairingState = this._pairingState ?? "idle";
                if (name === "trust.controllers.list") {
                    responseBody = { peers: this._trustedPeers };
                } else if (name === "trust.pair.begin") {
                    this._pairingState = "awaiting_approval";
                    this._pairingFingerprint = "A3F9-21C4";
                    this._pairingCode = 42913;
                    responseBody = { ok: true };
                } else if (name === "trust.pair.cancel") {
                    this._pairingState = "idle";
                    responseBody = { ok: true };
                } else if (name === "trust.pair.status") {
                    responseBody = { state: this._pairingState };
                    if (this._pairingState === "awaiting_approval") {
                        responseBody.fingerprint = this._pairingFingerprint;
                        responseBody.numeric_code = this._pairingCode;
                    }
                } else if (name === "trust.controller.forget") {
                    const before = this._trustedPeers.length;
                    this._trustedPeers = this._trustedPeers.filter((p) => p.fingerprint !== body.fingerprint);
                    responseBody = { ok: this._trustedPeers.length < before };
                } else {
                    isError = true;
                }
            } else if (envelope.serviceId === 7) {
```

(this replaces the standalone `if (envelope.serviceId === 7) {` at line 375 with `} else if (envelope.serviceId === 7) {` as shown, chaining onto the new Trust branch above it — the rest of the existing Gateway branch's body is unchanged.)

- [ ] **Step 2: Extend `ConfigureFakeDevicesAsync`'s config shape in `TestWorld.cs`** so scenarios can seed a starting trust list, alongside the existing `authorizedCount`/`unauthorizedCount` parameters:

```csharp
public async Task ConfigureFakeDevicesAsync(
    int authorizedCount, int unauthorizedCount = 0, IReadOnlyList<object>? trustedPeers = null)
{
    var configs = new List<object>();
    for (var i = 0; i < authorizedCount; i++)
    {
        configs.Add(new
        {
            id = $"fake-{i + 1}", authorized = true, firmware = DefaultFirmware, device = "EspScreenBarcodeGenerator",
            trustedPeers = trustedPeers ?? Array.Empty<object>(),
        });
    }
    for (var i = 0; i < unauthorizedCount; i++)
        configs.Add(new { id = $"fake-unauth-{i + 1}", authorized = false, firmware = DefaultFirmware });

    var json = JsonSerializer.Serialize(configs);
    await Page.AddInitScriptAsync($"window.__espFakeSerialConfig = {json};");
    await Page.GotoAsync(AppServer.BaseUrl);
    await Page.GetByText("ESP Barcode Control").First.WaitForAsync();
}
```

- [ ] **Step 3: Add scenarios to `Gateway.feature`**, matching this file's existing "for that device" phrasing (lines 10–31):

```gherkin
  Scenario: Trust card starts with no paired devices
    Given I put the first device into gateway mode
    When I open the Gateway page
    Then the Gateway page shows no trusted devices for that device

  Scenario: Pairing a new device shows the on-device approval code
    Given I put the first device into gateway mode
    When I open the Gateway page
    And I click "Ping for Clients" for that device
    And I click "Pair new device" for that device
    Then the Gateway page shows a pairing code for that device

  Scenario: Forgetting a trusted device removes it from the list
    Given I put the first device into gateway mode with a trusted device "A3F9-21C4"
    When I open the Gateway page
    And I click "Forget" for that device
    Then the Gateway page shows no trusted devices for that device
```

- [ ] **Step 4: Implement the step definitions** in `GatewayStepDefinitions.cs`, matching the existing method/locator style exactly:

```csharp
    [Given("I put the first device into gateway mode with a trusted device {string}")]
    public async Task GivenFirstDeviceHasTrustedDevice(string fingerprint)
    {
        await world.ConfigureFakeDevicesAsync(authorizedCount: 2, trustedPeers:
            [new { fingerprint, mac = "AA:BB:CC:DD:EE:02", route_id = 1, paired_at_ms = 0, label = "" }]);
        await WhenIPutFirstDeviceIntoGatewayMode();
    }

    [When("I click \"Pair new device\" for that device")]
    public async Task WhenIClickPairNewDevice()
    {
        await Page.Locator("[data-testid=gateway-pair-new-device]").First.ClickAsync();
        await Task.Delay(1200); // matches the poll timer's ~1s cadence (Task 10 Step 3)
    }

    [When("I click \"Forget\" for that device")]
    public async Task WhenIClickForget()
    {
        await Page.Locator("[data-testid=gateway-trust-forget]").First.ClickAsync();
        await Task.Delay(300);
    }

    [Then("the Gateway page shows no trusted devices for that device")]
    public async Task ThenGatewayShowsNoTrustedDevices()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-trust-empty]").First).ToBeVisibleAsync();

    [Then("the Gateway page shows a pairing code for that device")]
    public async Task ThenGatewayShowsPairingCode()
        => await Assertions.Expect(Page.Locator("[data-testid=gateway-pairing-code]").First).ToBeVisibleAsync();
```

(constructor injection already gives this class `world`/`Page` — see line 8–10 — so `world.ConfigureFakeDevicesAsync` and the existing `WhenIPutFirstDeviceIntoGatewayMode` are directly reusable.)

- [ ] **Step 5: Run the E2E suite and verify the new scenarios pass**

Run: `dotnet test dotnet/tests/EspBarcode.Controller.Web.E2ETests` (per `dotnet/README.md`'s "E2E tests" section — spins up the dev server and drives it headlessly via Reqnroll + Playwright)
Expected: all scenarios pass, including the three new Trust ones, with no regression in the existing Gateway scenarios.

- [ ] **Step 6: Commit**

```bash
git add dotnet/tests/EspBarcode.Controller.Web.E2ETests/Features/Gateway.feature \
        dotnet/tests/EspBarcode.Controller.Web.E2ETests/StepDefinitions/GatewayStepDefinitions.cs \
        dotnet/tests/EspBarcode.Controller.Web.E2ETests/Support/TestWorld.cs \
        dotnet/src/EspBarcode.Controller.Web/wwwroot/js/fakeSerial.js
git commit -m "test(trust): add E2E coverage for the Blazor Trust card"
```
