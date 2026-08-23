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
    // send). Returns false and calls reset() (state -> Idle) if the peer's self-signature doesn't
    // verify. Returns false with state unchanged if an attempt is already past Idle/AwaitingPeerHello.
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
    // and calls reset() (state -> Idle) on a bad signature.
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
