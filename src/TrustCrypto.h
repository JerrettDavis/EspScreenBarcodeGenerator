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
