#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationPorts.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "Envelope.h"
#include "FrameAssembler.h"
#include "HopFrame.h"
#include "TrustConfigStore.h"
#include "TrustCrypto.h"
#include "TrustPairingSession.h"

namespace esplink {

// Native ESP-NOW carrier endpoint: the display-side compatibility profile described in
// docs/PROTOCOL_V2.md §5/§8 (`CarrierProfileId::EspNowV1`, 250-byte datagram ceiling).
// Each ESP-NOW payload *is* one raw hop frame — no COBS delimiting, since ESP-NOW already
// delivers discrete datagrams. Messages larger than one frame's ~214-byte payload budget
// are split with `fragmentIntoHopFrames` and reassembled with the same `FrameAssembler`
// every other endpoint uses.
//
// `onReceive`/`onSent` are free functions (ESP-NOW's C callback API takes no user context
// pointer) that forward into the single endpoint instance via `instance()`. The receive
// callback runs on the Wi-Fi/ESP-NOW task, not the Arduino loop task: it only copies bytes
// into a bounded, statically-sized queue — no parsing, allocation, or engine dispatch
// happens there. `loop()` drains that queue on the main task.
//
// Also owns this board's half of the gateway-discovery ping/pong (ServiceId::Gateway,
// "gateway.link.ping"/"gateway.link.pong", docs/PROTOCOL_V2.md §10): while this endpoint is
// running (i.e. the board has NOT been switched into gateway relay mode), it periodically
// broadcasts a "looking for a gateway" ping if it hasn't heard one recently, and always
// answers any gateway-originated ping with a pong carrying its own identity. See
// GatewayRelay for the gateway-side half of the same exchange.
class EspNowEndpoint : public IControlResponseSink, public IGatewayLinkStatusSource {
public:
    EspNowEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device);

    // Initializes Wi-Fi (station mode, no AP join), esp_now, registers callbacks, and adds
    // the broadcast address as an unencrypted peer. Returns false and leaves `error` set on
    // failure; the caller decides whether that's fatal.
    bool begin(std::string& error);

    void loop();

    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

    // The MAC address this radio is broadcasting from, formatted "AA:BB:CC:DD:EE:FF".
    // Empty until `begin()` succeeds.
    const std::string& macAddress() const { return macAddress_; }

    static constexpr std::size_t kMaxDatagramBytes = 250;    // ESP_NOW_MAX_DATA_LEN
    static constexpr std::size_t kRxQueueCapacity = 8;
    static EspNowEndpoint* instance() { return instance_; }

    // Called from the ESP-NOW receive callback (Wi-Fi task context). Bounded, allocation-free.
    void enqueueReceived(const uint8_t* mac, const uint8_t* data, std::size_t length);

    // IGatewayLinkStatusSource — surfaced through the ordinary `status` command
    // (see ControlProtocolEngine::setGatewayLinkStatusSource).
    GatewayLinkInfo gatewayLinkStatus() const override;

    // Secure Pairing (docs/superpowers/specs/2026-08-22-espnow-secure-pairing-design.md) — the
    // on-device Trust screen (Task 8) drives pairing through these, and reads trust state
    // through trustedPeers()/fingerprintList()/macList()/forgetByFingerprint() below.
    enum class TrustPairingUiState : uint8_t { Idle, Discovering, AwaitingApproval, Committed, Cancelled };
    struct TrustPairingUiStatus {
        TrustPairingUiState state = TrustPairingUiState::Idle;
        std::string peerFingerprint;
        // Non-empty/non-zero ONLY while a local human tap is still outstanding -- see
        // pairingStatus(), which sources them from currentOutcome()'s AwaitingApproval-only
        // guard. A trusted-peer reconnect auto-confirms and therefore never populates them, which
        // is what keeps it silent on-screen.
        uint32_t numericCode = 0;
    };

    // Starts pairing with a specific MAC seen via the existing discovery ping/pong (see
    // gatewayLinkStatus()/lastGatewayId_ -- the on-device UI, Task 8, offers this as "the
    // currently-discovered gateway" since EspNowEndpoint is inherently 1:1 with a single gateway
    // at a time).
    bool beginPairing(const std::array<uint8_t, 6>& targetMac);
    void confirmPairing();
    void denyPairing();
    TrustPairingUiStatus pairingStatus() const;
    const TrustStore& trustedPeers() const { return trustConfig_.store(); }
    // Looks up a record by its displayed fingerprint (same format trustFingerprint() produces)
    // and forgets it. Returns false if no record matches. Used by the on-device Trust screen's
    // "Forget" button (Task 8), which only has the fingerprint string shown on screen, not the
    // raw public key.
    bool forgetByFingerprint(const std::string& fingerprint);
    // One fingerprint string per trusted record, same order as trustedPeers() -- the on-device
    // Trust screen (Task 8) renders this directly instead of recomputing fingerprints itself,
    // since that needs trustCrypto_'s sha256(), which is private to this class.
    std::vector<std::string> fingerprintList() const;
    // One MAC per trusted record, same order/index as fingerprintList() -- the on-device Trust
    // screen (Task 8) zips the two together into TrustPeerRow so it can compare a discovered
    // peer's MAC against already-trusted MACs directly, without string-comparing fingerprints.
    std::vector<std::array<uint8_t, 6>> macList() const;
    // Refuses to enable Secure Pairing when trust init failed at begin() -- enforcement reads the
    // trust store on every inbound message, so turning it on without a working store would drop
    // all traffic with no way to pair anything back. Disabling is always allowed, so a device
    // that lost its trust store can still be returned to the open compatibility profile.
    bool setSecurePairingEnabled(bool value, std::string& error) {
        if (value && !trustReady_) {
            error = "trust store unavailable (trust init failed at startup)";
            return false;
        }
        return trustConfig_.setSecurePairingEnabled(value, error);
    }
    bool securePairingEnabled() const { return trustConfig_.securePairingEnabled(); }

private:
    struct RxDatagram {
        std::array<uint8_t, kMaxDatagramBytes> bytes{};
        std::array<uint8_t, 6> mac{};
        std::size_t length = 0;
    };

    void processDatagram(const RxDatagram& datagram);
    void processMessage(const std::array<uint8_t, 6>& fromMac, const MessageEnvelope& envelope,
                        const std::vector<uint8_t>& body);
    void sendEnvelopeTo(const uint8_t* destination, MessageKind kind, ServiceId serviceId,
                       const std::vector<uint8_t>& bodyBytes, uint64_t correlationId);

    // The gateway-discovery ping/pong side channel — handled independently of the ordinary
    // command dispatch path above (it's Event-kind, not Command, and needs no ControlSession).
    void handleGatewayLinkMessage(JsonObjectConst wrapper);
    void maybeSendGatewayProbe();
    void sendGatewayLinkEvent(const char* eventName, uint32_t echoTs);

    // Trust/pairing handshake handling (ServiceId::Trust, "trust.pair.begin/confirm/cancel") --
    // handled independently of the ordinary command dispatch path, same rationale as the
    // gateway-discovery side channel above.
    void handleTrustMessage(const uint8_t* fromMac, JsonObjectConst wrapper);
    void sendTrustHello(const std::array<uint8_t, 6>& toMac, const TrustHelloMessage& hello);
    void sendTrustConfirm(const std::array<uint8_t, 6>& toMac, const TrustSignature& signature);
    void sendTrustCancel(const std::array<uint8_t, 6>& toMac);
    bool ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac);
    bool upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac, const std::array<uint8_t, kTrustLmkBytes>& lmk);
    // Deletes the ESP-NOW peer-table entry for `mac` unless it belongs to an already-established
    // trust record -- some *other*, successful pairing put it there, and it must survive this
    // attempt's failure/cancellation/timeout undisturbed. Used by every path that ends a pairing
    // attempt without a successful commit (timeout, deny, incoming cancel, a rejected hello, a
    // failed peer registration, a failed persist).
    void evictUntrustedPeer(const std::array<uint8_t, 6>& mac);
    // Called once trustPairing_ has reached Committed (from confirmPairing() or an incoming
    // trust.pair.confirm) to persist the record and switch the peer over to encrypted comms. If
    // persistence fails, this is treated as a failed attempt rather than silently proceeding as
    // if it had succeeded: nothing gets encrypted, the temporary peer entry is evicted, and the
    // session moves to Cancelled instead of staying Committed.
    void finalizePairingCommit(const std::array<uint8_t, 6>& mac);

    static const char* mapV2Name(const std::string& name);

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    const IBarcodeDevice& device_;
    FrameAssembler assembler_;
    std::string macAddress_;

    uint32_t linkMessageCounter_ = 1;
    uint64_t nextResponseOperationId_ = 1;
    uint64_t currentRequestOperationId_ = 0;
    std::string currentRequestName_;

    // Gateway-discovery state (this board's "client" role) — see the class comment above.
    static constexpr uint32_t kGatewayProbeIntervalMs = 3000;    // while not connected
    static constexpr uint32_t kGatewayKeepaliveIntervalMs = 8000; // while connected, to confirm liveness
    static constexpr uint32_t kGatewayLinkTimeoutMs = 6000;
    uint32_t lastGatewaySeenMs_ = 0;   // millis() of the last gateway ping/pong seen; 0 = never
    uint32_t lastGatewayRttMs_ = 0;
    std::string lastGatewayId_;
    uint32_t lastProbeSentMs_ = 0;
    uint32_t lastProbeTs_ = 0;         // the `ts` this board embedded in its most recent probe

    // Trust/pairing state (Secure Pairing) — persisted trust records/identity plus this
    // attempt's in-progress handshake state machine, if any. trustCrypto_ must be declared
    // before trustPairing_ (its default member initializer captures a reference to trustCrypto_,
    // and default member initializers run in declaration order).
    TrustConfigStore trustConfig_;
    TrustCrypto trustCrypto_;
    TrustPairingSession trustPairing_{trustCrypto_};
    std::array<uint8_t, 6> pairingTargetMac_{};
    bool pairingIsInitiator_ = false;
    // False if trust crypto/persistence failed to initialize in begin(). ESP-NOW still runs
    // (Secure Pairing is opt-in), but Secure Pairing cannot be enabled -- see
    // setSecurePairingEnabled.
    bool trustReady_ = false;

    // Fixed-capacity ring buffer filled by the ESP-NOW receive callback, drained by loop().
    // A full queue drops the newest datagram rather than growing — bounded memory per
    // docs' "no unbounded reassembly"/"no dynamic allocation from ESP-NOW callbacks" rule.
    std::array<RxDatagram, kRxQueueCapacity> rxQueue_{};
    volatile std::size_t rxHead_ = 0;  // next slot loop() reads
    volatile std::size_t rxTail_ = 0;  // next slot the callback writes
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;

    static EspNowEndpoint* instance_;
};

}  // namespace esplink
