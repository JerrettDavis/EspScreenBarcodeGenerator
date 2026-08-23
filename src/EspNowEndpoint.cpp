#include "EspNowEndpoint.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>

#include "EspBarcodeCore.h"  // bytesToBase64/bytesFromBase64
#include "Fragmenter.h"
#include "JsonCommandCodec.h"

using namespace espbarcode;

namespace esplink {

namespace {
// ESP-NOW requires both peers to share a fixed radio channel before pairing; there is no
// channel-negotiation handshake in this compatibility profile. Broadcasting on the
// broadcast address means any nearby peer on this channel can reach this endpoint at the
// transport level; command-level access (everything except ServiceId::Trust/Gateway) is
// restricted to already-trusted senders once Secure Pairing is enabled -- see the enforcement
// check in processMessage() and TrustConfigStore::securePairingEnabled().
constexpr uint8_t kEspNowChannel = 1;
const uint8_t kBroadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

std::string formatMac(const uint8_t mac[6]) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return std::string(buffer);
}
}  // namespace

EspNowEndpoint* EspNowEndpoint::instance_ = nullptr;

namespace {
void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int length) {
    if (EspNowEndpoint* self = EspNowEndpoint::instance()) {
        if (length > 0) self->enqueueReceived(mac, data, static_cast<std::size_t>(length));
    }
}

void onEspNowSent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {
    // Fire-and-forget for this compatibility profile: no application-level ACK/retry is
    // implemented yet (docs/PROTOCOL_V2.md §10 lists ESP-NOW reliability as future work).
}
}  // namespace

EspNowEndpoint::EspNowEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device)
    : engine_(engine), session_(session), device_(device) {
    instance_ = this;
}

bool EspNowEndpoint::begin(std::string& error) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        error = "failed to set espnow channel";
        return false;
    }
    if (esp_now_init() != ESP_OK) {
        error = "esp_now_init failed";
        return false;
    }
    esp_now_register_recv_cb(onEspNowReceive);
    esp_now_register_send_cb(onEspNowSent);

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, kBroadcastAddress, 6);
    peer.channel = kEspNowChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        error = "esp_now_add_peer failed";
        return false;
    }

    uint8_t mac[6];
    WiFi.macAddress(mac);
    macAddress_ = formatMac(mac);

    // Trust crypto must begin() only after Wi-Fi/ESP-NOW is up (see TrustCrypto.h -- esp_random()
    // needs the radio initialized to be a true hardware RNG), which is exactly where we are now.
    //
    // A failure here is deliberately NOT fatal to the endpoint, matching GatewayRelay::begin()'s
    // stance. Secure Pairing is opt-in and off by default, so a LittleFS mount failure, a full
    // NVS partition, or a failed identity write must not take down plain ESP-NOW on a device that
    // never asked for pairing -- that would be a regression against the previously
    // hardware-validated compatibility profile. It only means Secure Pairing cannot be turned on
    // (see setSecurePairingEnabled, which refuses while trustReady_ is false).
    std::string trustError;
    trustReady_ = trustCrypto_.begin(trustError) && trustConfig_.begin(trustCrypto_, trustError);
    if (!trustReady_) {
        Serial.printf("{\"event\":\"trust_init_failed\",\"transport\":\"espnow\",\"message\":\"%s\"}\n",
                      trustError.c_str());
    }
    return true;
}

void EspNowEndpoint::enqueueReceived(const uint8_t* mac, const uint8_t* data, std::size_t length) {
    if (length > kMaxDatagramBytes) return;  // cannot happen from a real ESP-NOW radio; defensive only

    portENTER_CRITICAL(&rxMux_);
    const std::size_t nextTail = (rxTail_ + 1) % kRxQueueCapacity;
    if (nextTail == rxHead_) {
        // Queue full: drop the newest datagram rather than grow unboundedly.
        portEXIT_CRITICAL(&rxMux_);
        return;
    }
    RxDatagram& slot = rxQueue_[rxTail_];
    memcpy(slot.bytes.data(), data, length);
    memcpy(slot.mac.data(), mac, slot.mac.size());
    slot.length = length;
    rxTail_ = nextTail;
    portEXIT_CRITICAL(&rxMux_);
}

void EspNowEndpoint::loop() {
    for (;;) {
        portENTER_CRITICAL(&rxMux_);
        if (rxHead_ == rxTail_) {
            portEXIT_CRITICAL(&rxMux_);
            break;
        }
        RxDatagram datagram = rxQueue_[rxHead_];
        rxHead_ = (rxHead_ + 1) % kRxQueueCapacity;
        portEXIT_CRITICAL(&rxMux_);

        processDatagram(datagram);
    }
    maybeSendGatewayProbe();

    // An in-progress pairing attempt that nobody ever confirmed/denied (e.g. a human never
    // returned to the Trust screen, or a peer went silent mid-handshake) must not block all
    // future pairing forever -- tick() moves it to Cancelled once it's been stuck too long, and
    // whatever temporary peer-table entry belonged to that attempt is evicted here rather than
    // left to exhaust ESP-NOW's small peer-table ceiling.
    if (trustPairing_.tick(millis())) {
        evictUntrustedPeer(pairingTargetMac_);
    }
}

void EspNowEndpoint::processDatagram(const RxDatagram& datagram) {
    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(datagram.bytes.data(), datagram.length, header, payload, frameError)) return;

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = assembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    MessageEnvelope envelope;
    std::vector<uint8_t> body;
    CodecError envelopeError;
    if (!decodeEnvelope(assembled.data(), assembled.size(), envelope, body, envelopeError)) return;

    processMessage(datagram.mac, envelope, body);
}

void EspNowEndpoint::processMessage(const std::array<uint8_t, 6>& fromMac, const MessageEnvelope& envelope,
                                    const std::vector<uint8_t>& body) {
    JsonDocument document;
    if (deserializeJson(document, body.data(), body.size())) return;
    JsonObjectConst wrapper = document.as<JsonObjectConst>();

    if (envelope.serviceId == ServiceId::Gateway) {
        // Discovery ping/pong is a separate, Event-kind side channel — never dispatched
        // through ControlProtocolEngine (see the class comment above).
        handleGatewayLinkMessage(wrapper);
        return;
    }

    if (envelope.serviceId == ServiceId::Trust) {
        // Pairing handshake traffic is likewise a separate side channel, and must always flow
        // regardless of the Secure Pairing enforcement check below (it's how trust gets
        // established in the first place).
        handleTrustMessage(fromMac.data(), wrapper);
        return;
    }

    // Secure Pairing enforcement: once enabled, only a sender whose MAC already has a trust
    // record is allowed through to command dispatch. Trust/Gateway traffic is exempt (handled
    // above already, before this point is ever reached) since it's needed to establish trust
    // and keep gateway discovery working regardless of pairing state.
    if (trustConfig_.securePairingEnabled() && trustConfig_.store().findByMac(fromMac) == nullptr) {
        return;  // not a trusted peer: drop
    }

    if (envelope.kind != MessageKind::Command) return;  // this endpoint only accepts commands from a controller

    const char* name = wrapper["name"] | "";
    JsonObjectConst innerBody = wrapper["body"].as<JsonObjectConst>();

    const char* v1Name = mapV2Name(name);
    currentRequestOperationId_ = envelope.operationId;
    currentRequestName_ = name;

    if (v1Name == nullptr) {
        sendError(ProtocolError{name, "unknown_command", "command not supported over EspLink v2 this release"});
        return;
    }

    Command command;
    std::string errorCode, errorMessage;
    if (!JsonCommandCodec::decode(v1Name, innerBody, device_, command, errorCode, errorMessage)) {
        sendError(ProtocolError{name, errorCode, errorMessage});
        return;
    }

    engine_.handle(session_, command, OperationId{envelope.operationId}, "espnow-v1", *this);
}

void EspNowEndpoint::handleGatewayLinkMessage(JsonObjectConst wrapper) {
    const char* name = wrapper["name"] | "";
    JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();
    const char* role = body["role"] | "";
    if (std::strcmp(role, "gateway") != 0) return;  // ignore chatter from other clients

    lastGatewaySeenMs_ = millis();
    lastGatewayId_ = std::string(body["deviceId"] | "");

    if (std::strcmp(name, "gateway.link.ping") == 0) {
        // A gateway is broadcasting for prospective clients — echo its ts back so it can
        // compute RTT, and reply so it can list us as a discovered peer.
        const uint32_t ts = body["ts"] | 0;
        sendGatewayLinkEvent("gateway.link.pong", ts);
    } else if (std::strcmp(name, "gateway.link.pong") == 0) {
        // Reply to our own outbound probe (maybeSendGatewayProbe) — echoTs is the ts we sent.
        const uint32_t echoTs = body["echoTs"] | 0;
        const uint32_t now = millis();
        if (echoTs != 0 && now >= echoTs) lastGatewayRttMs_ = now - echoTs;
    }
}

void EspNowEndpoint::maybeSendGatewayProbe() {
    const uint32_t now = millis();
    const bool connected = lastGatewaySeenMs_ != 0 && (now - lastGatewaySeenMs_) <= kGatewayLinkTimeoutMs;
    const uint32_t interval = connected ? kGatewayKeepaliveIntervalMs : kGatewayProbeIntervalMs;
    if (lastProbeSentMs_ != 0 && (now - lastProbeSentMs_) < interval) return;

    lastProbeSentMs_ = now;
    lastProbeTs_ = now;
    sendGatewayLinkEvent("gateway.link.ping", 0);
}

void EspNowEndpoint::sendGatewayLinkEvent(const char* eventName, uint32_t echoTs) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = eventName;
    JsonObject body = wrapper["body"].to<JsonObject>();
    body["role"] = "client";
    body["deviceId"] = macAddress_;
    body["firmware"] = ESPBARCODE_VERSION;
    body["ts"] = millis();
    if (echoTs != 0) body["echoTs"] = echoTs;

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(kBroadcastAddress, MessageKind::Event, ServiceId::Gateway, bodyBytes, 0);
}

GatewayLinkInfo EspNowEndpoint::gatewayLinkStatus() const {
    GatewayLinkInfo info;
    if (lastGatewaySeenMs_ == 0) return info;  // never seen a gateway — defaults are correct
    const uint32_t now = millis();
    info.ageMs = now - lastGatewaySeenMs_;
    info.connected = info.ageMs <= kGatewayLinkTimeoutMs;
    info.rttMs = lastGatewayRttMs_;
    info.gatewayId = lastGatewayId_;
    return info;
}

// Same v2-name -> v1-name subset SerialCobsEndpoint maps (docs/PROTOCOL_V2.md §7) — the
// ESP-NOW endpoint drives the identical ControlProtocolEngine handlers, just over a
// smaller-frame carrier.
const char* EspNowEndpoint::mapV2Name(const std::string& name) {
    if (name == "system.hello" || name == "system.ping") return "hello";
    if (name == "barcode.generate") return "generate";
    if (name == "barcode.close") return "close";
    if (name == "device.backlight.set") return "backlight";
    if (name == "device.orientation.set") return "orientation";
    return nullptr;
}

void EspNowEndpoint::sendEnvelopeTo(const uint8_t* destination, MessageKind kind, ServiceId serviceId,
                                    const std::vector<uint8_t>& bodyBytes, uint64_t correlationId) {
    MessageEnvelope envelope;
    envelope.kind = kind;
    envelope.serviceId = serviceId;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = session_.id().value;
    envelope.operationId = nextResponseOperationId_++;
    envelope.correlationId = correlationId;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::EspNowV1;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), header, kMaxDatagramBytes, frames, fragmentError)) {
        return;
    }

    for (const auto& frame : frames) {
        esp_now_send(destination, frame.data(), frame.size());
    }
}

void EspNowEndpoint::send(const Response& response) {
    // system.hello gets a distinct response name/body per docs/PROTOCOL_V2.md §7, matching
    // SerialCobsEndpoint's behavior — every other mapped command echoes its own request name.
    const std::string responseName = (currentRequestName_ == "system.hello") ? "system.welcome" : currentRequestName_;

    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = responseName;
    JsonObject body = wrapper["body"].to<JsonObject>();
    if (responseName == "system.welcome") {
        body["deviceId"] = "esbg-espnow-v1";
        body["firmware"] = ESPBARCODE_VERSION;
        body["selectedVersion"] = "2.0";
        body["controlSessionId"] = session_.id().value;
        body["carrier"]["profile"] = "espnow-v1";
        body["carrier"]["maxFrameBytes"] = static_cast<uint32_t>(kMaxDatagramBytes);
    } else {
        JsonCommandCodec::encodeBody(response, body);
    }

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(kBroadcastAddress, MessageKind::Result, ServiceId::System, bodyBytes, currentRequestOperationId_);
}

void EspNowEndpoint::sendError(const ProtocolError& error) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = currentRequestName_;
    wrapper["error"]["code"] = error.code;
    wrapper["error"]["message"] = error.message;

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(kBroadcastAddress, MessageKind::Error, ServiceId::System, bodyBytes, currentRequestOperationId_);
}

namespace {
bool decodeHelloJson(JsonObjectConst body, TrustHelloMessage& out) {
    std::vector<uint8_t> bytes;
    if (!bytesFromBase64(body["staticPubKey"] | "", bytes) || bytes.size() != kTrustPublicKeyBytes) {
        return false;
    }
    std::memcpy(out.staticPublicKey.data(), bytes.data(), bytes.size());
    if (!bytesFromBase64(body["ephemeralPubKey"] | "", bytes) || bytes.size() != kTrustPublicKeyBytes) {
        return false;
    }
    std::memcpy(out.ephemeralPublicKey.data(), bytes.data(), bytes.size());
    if (!bytesFromBase64(body["nonce"] | "", bytes) || bytes.size() != kTrustNonceBytes) return false;
    std::memcpy(out.nonce.data(), bytes.data(), bytes.size());
    if (!bytesFromBase64(body["signature"] | "", bytes) || bytes.size() != kTrustSignatureBytes) {
        return false;
    }
    std::memcpy(out.signature.data(), bytes.data(), bytes.size());
    return true;
}

void encodeHelloJson(const TrustHelloMessage& hello, JsonObject body) {
    body["staticPubKey"] =
        bytesToBase64(std::vector<uint8_t>(hello.staticPublicKey.begin(), hello.staticPublicKey.end()));
    body["ephemeralPubKey"] =
        bytesToBase64(std::vector<uint8_t>(hello.ephemeralPublicKey.begin(), hello.ephemeralPublicKey.end()));
    body["nonce"] = bytesToBase64(std::vector<uint8_t>(hello.nonce.begin(), hello.nonce.end()));
    body["signature"] = bytesToBase64(std::vector<uint8_t>(hello.signature.begin(), hello.signature.end()));
}

// A terminal result (Committed/Cancelled) lingers in trustPairing_ so pairingStatus() can report
// it to whatever's polling -- see beginPairing()/handleTrustMessage's "trust.pair.begin" branch,
// which reset() a stale terminal result at the start of the *next* attempt rather than the
// moment it's reached.
bool isTerminalPairingState(TrustPairingState state) {
    return state == TrustPairingState::Committed || state == TrustPairingState::Cancelled;
}

bool isPairingInFlight(TrustPairingState state) {
    return state == TrustPairingState::AwaitingPeerHello || state == TrustPairingState::AwaitingApproval ||
           state == TrustPairingState::AwaitingPeerConfirm;
}
}  // namespace

bool EspNowEndpoint::ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac) {
    // An existing entry is only reusable if it is actually unencrypted. A leftover encrypt=true
    // entry -- left by an earlier successful pairing with this MAC whose peer has since rebooted,
    // been forgotten on the far side, or otherwise lost the matching LMK -- would make the radio
    // encrypt this handshake's plaintext hello under a key the peer can no longer decrypt, and
    // the handshake would die silently with no error on either side. Tear such an entry down and
    // re-add it unencrypted rather than reusing it, so a re-pair attempt actually gets through.
    // (finalizePairingCommit re-upgrades it via upgradeToEncryptedPeer once the attempt commits.)
    esp_now_peer_info_t existing{};
    if (esp_now_get_peer(mac.data(), &existing) == ESP_OK) {
        if (!existing.encrypt) return true;
        esp_now_del_peer(mac.data());
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;  // 0 = use the current channel, already fixed by begin()
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowEndpoint::upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac,
                                            const std::array<uint8_t, kTrustLmkBytes>& lmk) {
    esp_now_del_peer(mac.data());
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, lmk.data(), lmk.size());
    return esp_now_add_peer(&peer) == ESP_OK;
}

void EspNowEndpoint::evictUntrustedPeer(const std::array<uint8_t, 6>& mac) {
    if (trustConfig_.store().findByMac(mac) == nullptr) {
        esp_now_del_peer(mac.data());
    }
    // else: this mac has an established trust record -- some *other* successful pairing (past or
    // present) put this peer-table entry there, so a failed/cancelled/timed-out attempt touching
    // the same mac must not tear down a working encrypted connection.
}

void EspNowEndpoint::finalizePairingCommit(const std::array<uint8_t, 6>& mac) {
    const bool alreadyTrusted = trustConfig_.store().findByMac(mac) != nullptr;
    TrustRecord record;
    record.staticPublicKey = trustPairing_.peerHello().staticPublicKey;
    record.mac = mac;
    record.peerRole = TrustRole::Gateway;  // EspNowEndpoint always pairs with a gateway
    record.pairedAtMs = millis();
    std::string persistError;
    // A reconnect's record already exists; addRecord's "already paired" failure is expected and
    // harmless there -- the record is already persisted, only the session key needed refreshing.
    const bool persisted = trustConfig_.addRecord(record, persistError) || alreadyTrusted;
    if (!persisted) {
        // Trust store full, or the LittleFS write failed: do NOT proceed as if this had
        // succeeded -- nothing gets encrypted, and the failure must be visible to whatever polls
        // pairingStatus() rather than silently pretending to have committed.
        bool shouldSend = false;
        trustPairing_.cancel(shouldSend);
        if (shouldSend) sendTrustCancel(mac);
        evictUntrustedPeer(mac);
        return;
    }
    upgradeToEncryptedPeer(mac, trustPairing_.derivedKeys().lmk);
    // Deliberately not reset() here -- state() stays Committed so pairingStatus() can report
    // success; the next beginPairing()/incoming trust.pair.begin resets it (see there).
}

bool EspNowEndpoint::beginPairing(const std::array<uint8_t, 6>& targetMac) {
    // A stale terminal result from a *previous* attempt must not block a new one -- see
    // pairingStatus()'s comment on why Committed/Cancelled linger instead of auto-resetting.
    if (isTerminalPairingState(trustPairing_.state())) trustPairing_.reset();
    if (trustPairing_.state() != TrustPairingState::Idle) return false;
    if (!ensureUnencryptedPeer(targetMac)) return false;

    TrustHelloMessage hello;
    if (!trustPairing_.beginAsInitiator(trustConfig_.identity(), millis(), hello)) return false;
    pairingTargetMac_ = targetMac;
    pairingIsInitiator_ = true;
    sendTrustHello(targetMac, hello);
    return true;
}

void EspNowEndpoint::confirmPairing() {
    TrustSignature signature{};
    if (!trustPairing_.confirmLocally(millis(), signature)) return;
    sendTrustConfirm(pairingTargetMac_, signature);
    if (trustPairing_.state() == TrustPairingState::Committed) {
        finalizePairingCommit(pairingTargetMac_);
    }
}

void EspNowEndpoint::denyPairing() {
    bool shouldSend = false;
    trustPairing_.cancel(shouldSend);
    if (shouldSend) sendTrustCancel(pairingTargetMac_);
    evictUntrustedPeer(pairingTargetMac_);
    // Deliberately not reset() here -- state() stays Cancelled so pairingStatus() can report it;
    // the next beginPairing()/incoming trust.pair.begin resets it (see there).
}

EspNowEndpoint::TrustPairingUiStatus EspNowEndpoint::pairingStatus() const {
    TrustPairingUiStatus status;
    TrustPairingOutcome outcome;
    switch (trustPairing_.state()) {
        case TrustPairingState::Idle:
            status.state = TrustPairingUiState::Idle;
            break;
        case TrustPairingState::AwaitingPeerHello:
            status.state = TrustPairingUiState::Discovering;
            break;
        case TrustPairingState::AwaitingApproval:
        case TrustPairingState::AwaitingPeerConfirm:
            status.state = TrustPairingUiState::AwaitingApproval;
            if (trustPairing_.currentOutcome(outcome)) {
                TrustHash hash{};
                trustCrypto_.sha256(outcome.peerStaticPublicKey.data(), outcome.peerStaticPublicKey.size(), hash);
                status.peerFingerprint = trustFingerprint(hash);
                status.numericCode = outcome.numericCode;
            }
            break;
        case TrustPairingState::Committed:
            status.state = TrustPairingUiState::Committed;
            break;
        case TrustPairingState::Cancelled:
            status.state = TrustPairingUiState::Cancelled;
            break;
    }
    return status;
}

std::vector<std::string> EspNowEndpoint::fingerprintList() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const TrustRecord* record = trustConfig_.store().at(i);
        TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        out.push_back(trustFingerprint(hash));
    }
    return out;
}

std::vector<std::array<uint8_t, 6>> EspNowEndpoint::macList() const {
    std::vector<std::array<uint8_t, 6>> out;
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) out.push_back(trustConfig_.store().at(i)->mac);
    return out;
}

bool EspNowEndpoint::forgetByFingerprint(const std::string& fingerprint) {
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const TrustRecord* record = trustConfig_.store().at(i);
        TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        if (trustFingerprint(hash) != fingerprint) continue;

        // Both copied before forgetRecord(): TrustStore::forget() is a swap-remove, so `record`
        // either dangles or points at an unrelated peer's record the moment it returns.
        const TrustPublicKey key = record->staticPublicKey;
        const std::array<uint8_t, 6> mac = record->mac;

        std::string error;
        if (!trustConfig_.forgetRecord(key, error)) return false;

        // The ESP-NOW peer-table entry outlives the trust record unless it's explicitly removed,
        // still registered with encrypt=true and the now-revoked LMK. Leaving it behind breaks
        // the spec's "Forget forces re-pairing on next contact" guarantee: the stale entry would
        // encrypt a later re-pair's hello under a key the peer no longer has. The return value is
        // ignored on purpose -- "no such peer" is a perfectly normal outcome here (e.g. the
        // record was only ever loaded from disk and never had a live entry this boot).
        esp_now_del_peer(mac.data());
        return true;
    }
    return false;
}

void EspNowEndpoint::handleTrustMessage(const uint8_t* fromMac, JsonObjectConst wrapper) {
    std::array<uint8_t, 6> mac{};
    std::memcpy(mac.data(), fromMac, 6);
    const char* name = wrapper["name"] | "";
    JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();

    if (std::strcmp(name, "trust.pair.begin") == 0) {
        TrustHelloMessage peerHello;
        if (!decodeHelloJson(body, peerHello)) return;

        // A stale terminal result from a *previous* attempt must not block a fresh one -- see
        // pairingStatus()'s comment on why Committed/Cancelled linger instead of auto-resetting.
        if (isTerminalPairingState(trustPairing_.state())) trustPairing_.reset();

        // If we're already mid-handshake as the initiator, only the peer we started with may
        // complete it -- trustPairing_.onPeerHello()'s AwaitingPeerHello branch unconditionally
        // accepts whatever hello it's given, so without this check a third MAC could hijack (and
        // destroy) our own in-flight attempt just by sending an unrelated hello of its own.
        if (trustPairing_.state() == TrustPairingState::AwaitingPeerHello && pairingTargetMac_ != mac) {
            return;
        }

        // A peer we already trust: verify the incoming hello against the *pinned* key on file,
        // not just its own self-consistency signature -- a MAC-spoofing attacker presenting a
        // different static key must be rejected outright rather than "verified against itself"
        // (see verifyTrustHello's doc comment).
        const TrustRecord* existing = trustConfig_.store().findByMac(mac);
        if (existing != nullptr && !verifyTrustHello(trustCrypto_, existing->staticPublicKey, peerHello)) {
            return;
        }

        // Was this device already mid-handshake as the initiator, specifically awaiting this
        // peer's reply? If so, this hello must be folded into *that* attempt --
        // trustPairing_.onPeerHello() below, called while in AwaitingPeerHello, does exactly
        // this using the ephemeral we already generated in beginPairing(). Generating a brand
        // new ephemeral for every incoming hello instead (rather than reusing that one) would
        // make the two sides derive a different session key on every round trip and never
        // converge -- this check is what keeps a reconnect handshake to a single round trip.
        const bool wasAwaitingOurOwnHello = pairingIsInitiator_ && pairingTargetMac_ == mac &&
                                            trustPairing_.state() == TrustPairingState::AwaitingPeerHello;

        // Captured *before* calling onPeerHello: it can return false for two different reasons,
        // and only one of them means an attempt was actually abandoned. From Idle (about to
        // become a fresh responder) or AwaitingPeerHello (we're the initiator awaiting exactly
        // this peer's reply -- guaranteed above), a signature failure makes onPeerHello reset()
        // internally, so evicting the peer entry below is correct. From any other state, a
        // *different* attempt is already in flight and onPeerHello's fallback `return false`
        // deliberately leaves that other attempt's state untouched (see TrustPairingSession's
        // contract) -- evicting in that case would delete a real, still-needed peer entry for
        // whichever peer we're actually mid-handshake with (see round-2 review finding).
        const bool preCallStateResetsOnFailure = trustPairing_.state() == TrustPairingState::Idle ||
                                                  trustPairing_.state() == TrustPairingState::AwaitingPeerHello;

        TrustHelloMessage reply;
        bool hasReply = false;
        if (!trustPairing_.onPeerHello(peerHello, trustConfig_.identity(), millis(), reply, hasReply)) {
            if (preCallStateResetsOnFailure) evictUntrustedPeer(mac);
            return;
        }

        // Only now -- after onPeerHello's self-consistency signature check has actually passed
        // -- do we register a peer-table entry for this sender. Registering unconditionally for
        // every incoming trust.pair.begin, before validating it, would let a flood of junk
        // hellos from distinct MACs exhaust ESP-NOW's small hardware peer-table ceiling; since
        // only one attempt can be in flight at a time (trustPairing_ is a single instance), at
        // most one such entry is ever at risk, and it's evicted on timeout/failure above.
        if (!ensureUnencryptedPeer(mac)) {
            bool ignoredShouldSend = false;
            trustPairing_.cancel(ignoredShouldSend);  // can't reply without a peer-table slot
            return;
        }

        if (!wasAwaitingOurOwnHello) {
            // We're the responder to this attempt (fresh or reconnect) -- remember who, for
            // confirmPairing()/denyPairing() and the auto-confirm below.
            pairingTargetMac_ = mac;
            pairingIsInitiator_ = false;
        }
        if (hasReply) sendTrustHello(mac, reply);

        // Reconnect (design spec §1 "Reconnect"): the peer is already trusted by this device, so
        // no human approval is needed on this side -- confirm immediately instead of waiting for
        // a UI tap. First-time pairing (existing == nullptr) still waits for confirmPairing().
        if (existing != nullptr) confirmPairing();
        return;
    }

    if (std::strcmp(name, "trust.pair.confirm") == 0) {
        if (mac != pairingTargetMac_) return;  // not the peer we're mid-handshake with

        std::vector<uint8_t> signatureBytes;
        if (!bytesFromBase64(body["confirmSignature"] | "", signatureBytes) ||
            signatureBytes.size() != kTrustSignatureBytes) {
            return;
        }
        TrustSignature signature{};
        std::memcpy(signature.data(), signatureBytes.data(), signature.size());
        if (!trustPairing_.onPeerConfirm(signature, millis())) {
            // Bad signature: onPeerConfirm() already reset() internally (state -> Idle). `mac`
            // is confirmed equal to pairingTargetMac_ by the guard above, so this attempt's own
            // peer entry -- not some unrelated one -- is what needs evicting here. Without this,
            // an attacker could register a peer via a self-consistent junk hello, then force a
            // reset-without-eviction by sending garbage as trust.pair.confirm, and repeat with
            // new MACs to exhaust ESP-NOW's small peer-table ceiling.
            evictUntrustedPeer(mac);
            return;
        }
        if (trustPairing_.state() == TrustPairingState::Committed) {
            finalizePairingCommit(mac);
        }
        return;
    }

    if (std::strcmp(name, "trust.pair.cancel") == 0) {
        // Only the peer we're actually mid-handshake with may cancel it, and only while an
        // attempt is genuinely in flight -- otherwise any nearby sender could tear down an
        // unrelated in-progress pairing attempt (or a just-completed one still waiting for
        // pairingStatus() to report it) with a stray message.
        if (mac == pairingTargetMac_ && isPairingInFlight(trustPairing_.state())) {
            bool ignoredShouldSend = false;
            trustPairing_.cancel(ignoredShouldSend);  // -> Cancelled; we're reacting to their
                                                       // cancel, so never send one back
            evictUntrustedPeer(mac);
        }
    }
}

void EspNowEndpoint::sendTrustHello(const std::array<uint8_t, 6>& toMac, const TrustHelloMessage& hello) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = "trust.pair.begin";
    encodeHelloJson(hello, wrapper["body"].to<JsonObject>());
    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(toMac.data(), MessageKind::Event, ServiceId::Trust, bodyBytes, 0);
}

void EspNowEndpoint::sendTrustConfirm(const std::array<uint8_t, 6>& toMac, const TrustSignature& signature) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = "trust.pair.confirm";
    wrapper["body"]["confirmSignature"] = bytesToBase64(std::vector<uint8_t>(signature.begin(), signature.end()));
    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(toMac.data(), MessageKind::Event, ServiceId::Trust, bodyBytes, 0);
}

void EspNowEndpoint::sendTrustCancel(const std::array<uint8_t, 6>& toMac) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = "trust.pair.cancel";
    wrapper["body"].to<JsonObject>();
    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelopeTo(toMac.data(), MessageKind::Event, ServiceId::Trust, bodyBytes, 0);
}

}  // namespace esplink
