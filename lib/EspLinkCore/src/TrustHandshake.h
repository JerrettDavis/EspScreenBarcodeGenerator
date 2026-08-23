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
