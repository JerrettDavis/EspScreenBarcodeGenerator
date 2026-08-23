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
