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

        std::array<uint8_t, kTrustPublicKeyBytes + kTrustPublicKeyBytes * 2 + kTrustNonceBytes * 2> transcript{};
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
        // Use only the first kTrustPrivateKeyBytes to ensure deterministic output regardless
        // of whether this is called with a 32-byte private key or 65-byte public key.
        const std::size_t keyBytesToUse = (keyLength < kTrustPrivateKeyBytes) ? keyLength : kTrustPrivateKeyBytes;
        for (std::size_t i = 0; i < keyBytesToUse; ++i) out[i % out.size()] ^= key[i];
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
