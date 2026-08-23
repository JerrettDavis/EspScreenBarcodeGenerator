#pragma once

#include <string>

#include "TrustCryptoPort.h"

namespace esplink {

// mbedTLS-backed ITrustCrypto for the ESP32 build (bundled with the Arduino core -- no new
// PlatformIO lib_deps entry). P-256 (secp256r1) for both ECDSA and ECDH; HKDF-SHA256 for
// derivation, built from mbedtls_md's HMAC-SHA256 per RFC 5869 (mbedTLS's own mbedtls_hkdf.h is
// not guaranteed present on every ESP-IDF/mbedTLS version this project has built against, so this
// implements HKDF directly from the always-present HMAC primitive instead of depending on it).
//
// Not thread-safe: every method (and the shared internal RNG state) must be called only from the
// main Arduino loop task, never from an ESP-NOW receive callback or any other interrupt/task
// context. This codebase's ESP-NOW endpoints already enforce this by only copying bytes in their
// receive callbacks and doing all real processing (including any crypto) from loop().
class TrustCrypto : public ITrustCrypto {
public:
    // Seeds the CTR-DRBG from the ESP32 hardware RNG (esp_random()). Call once per instance at
    // startup before any other method; returns false only if mbedTLS's entropy/DRBG setup fails.
    //
    // The DRBG is shared by every TrustCrypto instance in the process, so this is safe to call
    // from several of them: the first call seeds it, later calls reseed it in place (never
    // re-initialize it, which would zero the context out from under a live instance).
    //
    // Must be called after Wi-Fi or BT has been enabled (e.g. after WiFi.mode(WIFI_STA)/
    // esp_now_init()) for esp_random() to be a true hardware RNG rather than a weak pseudo-random
    // source -- see ESP-IDF's RNG documentation. The one-time boot self-test in main.cpp is an
    // exception: its keys are immediately discarded, so it seeds from whatever quality is
    // available before Wi-Fi/BLE/ESP-NOW init runs -- and the reseed on the *next* begin() (from
    // EspNowEndpoint/GatewayRelay, both of which run post-radio-init) is what gets real hardware
    // entropy in before any persistent identity key is generated.
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
};

}  // namespace esplink
