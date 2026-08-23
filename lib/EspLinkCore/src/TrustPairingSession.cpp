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
