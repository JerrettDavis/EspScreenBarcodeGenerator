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
