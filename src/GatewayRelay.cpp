#include "GatewayRelay.h"

#include <ArduinoJson.h>
#include <esp_now.h>

#include <cstdio>
#include <cstring>

#include "Cobs.h"
#include "EspBarcodeCore.h"  // bytesToBase64/bytesFromBase64
#include "Fragmenter.h"
#include "GatewayBridge.h"

using namespace espbarcode;

namespace esplink {

namespace {
// Must match the broadcast peer EspNowEndpoint::begin() already added -- GatewayRelay reuses
// that radio/peer registration and only takes over the receive callback.
const uint8_t kBroadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void onGatewayEspNowReceive(const uint8_t* mac, const uint8_t* data, int length) {
    if (GatewayRelay* self = GatewayRelay::instance()) {
        if (length > 0) self->enqueueFromEspNow(mac, data, static_cast<std::size_t>(length));
    }
}
}  // namespace

GatewayRelay* GatewayRelay::instance_ = nullptr;

GatewayRelay::GatewayRelay() { instance_ = this; }

void GatewayRelay::begin(std::string deviceId) {
    deviceId_ = std::move(deviceId);
    rxBlock_.reserve(256);
    // Trust crypto must begin() only after Wi-Fi/ESP-NOW is up (see TrustCrypto.h -- esp_random()
    // needs the radio initialized to be a true hardware RNG), which is guaranteed here since
    // GatewayRelay::begin() only ever runs after EspNowEndpoint::begin() has already brought up
    // Wi-Fi/esp_now (see the class comment above). A failure here is not treated as fatal to
    // relay operation -- it just means Secure Pairing can't be enabled until it succeeds (see
    // setSecurePairingEnabled, which refuses while trustReady_ is false) -- but it IS reported,
    // rather than silently discarded, so a broken trust store is visible in the serial log.
    std::string trustError;
    trustReady_ = trustCrypto_.begin(trustError) && trustConfig_.begin(trustCrypto_, trustError);
    if (!trustReady_) {
        Serial.printf("{\"event\":\"trust_init_failed\",\"transport\":\"gateway\",\"message\":\"%s\"}\n",
                      trustError.c_str());
    }
    esp_now_register_recv_cb(onGatewayEspNowReceive);
}

void GatewayRelay::pingNow() {
    lastDiscoveryPingMs_ = millis();
    sendGatewayLinkFrame("gateway.link.ping", 0);
}

void GatewayRelay::loop() {
    pollUsb();
    drainEspNowQueue();

    const uint32_t now = millis();
    if (lastDiscoveryPingMs_ == 0 || (now - lastDiscoveryPingMs_) >= kDiscoveryPingIntervalMs) {
        lastDiscoveryPingMs_ = now;
        sendGatewayLinkFrame("gateway.link.ping", 0);
    }

    // An in-progress pairing attempt that nobody ever confirmed/denied (e.g. the USB host never
    // polled trust.pair.status again, or a client went silent mid-handshake) must not block all
    // future pairing forever -- tick() moves it to Cancelled once it's been stuck too long, and
    // whatever temporary peer-table entry belonged to that attempt is evicted here rather than
    // left to exhaust ESP-NOW's small peer-table ceiling.
    if (trustPairing_.tick(now)) {
        evictUntrustedPeer(pairingTargetMac_);
    }
}

void GatewayRelay::pollUsb() {
    while (Serial.available() > 0) {
        linkStats_.recordHostActivity(millis());
        const uint8_t b = static_cast<uint8_t>(Serial.read());
        if (b == 0x00) {
            if (!rxBlock_.empty()) processUsbCobsBlock(rxBlock_);
            rxBlock_.clear();
            continue;
        }
        if (rxBlock_.size() < 4096) rxBlock_.push_back(b);  // bounded; oversized blocks are dropped at the delimiter
    }
}

void GatewayRelay::processUsbCobsBlock(const std::vector<uint8_t>& block) {
    std::vector<uint8_t> raw;
    if (!cobsDecode(block.data(), block.size(), raw)) return;  // malformed block: drop and resync on the next 0x00

    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(raw.data(), raw.size(), header, payload, frameError)) return;

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = usbAssembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    // A crack in the "never touches the envelope" relay purity (docs/PROTOCOL_V2.md's planned
    // gateway.* namespace, ServiceId::Gateway = 7): the host's own gateway.peers.list/
    // gateway.ping.now queries are about *this* board's local relay state, not the far-side
    // client's, so they're answered here directly instead of being relayed to ESP-NOW where
    // nothing would understand them. trust.* commands (ServiceId::Trust = 6) are the same kind
    // of crack -- pairing management is this board's own local state, not something a paired
    // client would understand either.
    MessageEnvelope envelope;
    std::vector<uint8_t> jsonBody;
    CodecError envError;
    const bool decoded = decodeEnvelope(assembled.data(), assembled.size(), envelope, jsonBody, envError);

    if (decoded && envelope.serviceId == ServiceId::Gateway) {
        if (handleGatewayServiceFromUsb(header, envelope, jsonBody)) return;
    }

    if (decoded && envelope.serviceId == ServiceId::Trust) {
        JsonDocument document;
        if (deserializeJson(document, jsonBody.data(), jsonBody.size())) return;  // malformed: drop, don't relay
        if (handleTrustFromUsb(header, envelope, document.as<JsonObjectConst>())) return;
    }

    relayToEspNow(header, assembled);
}

void GatewayRelay::enqueueFromEspNow(const uint8_t* mac, const uint8_t* data, std::size_t length) {
    // This relay accepts any sender this session (no pairing handshake); the MAC is only kept
    // for the gateway stats screen's "negotiated clients" list, recorded once the datagram is
    // drained on the main loop (see processEspNowDatagram) rather than here in callback context.
    if (length > kEspNowMaxDatagramBytes) return;  // cannot happen from a real ESP-NOW radio; defensive only

    portENTER_CRITICAL(&rxMux_);
    const std::size_t nextTail = (rxTail_ + 1) % kEspNowRxQueueCapacity;
    if (nextTail == rxHead_) {
        // Queue full: drop the newest datagram rather than growing unboundedly.
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

void GatewayRelay::drainEspNowQueue() {
    for (;;) {
        portENTER_CRITICAL(&rxMux_);
        if (rxHead_ == rxTail_) {
            portEXIT_CRITICAL(&rxMux_);
            break;
        }
        RxDatagram datagram = rxQueue_[rxHead_];
        rxHead_ = (rxHead_ + 1) % kEspNowRxQueueCapacity;
        portEXIT_CRITICAL(&rxMux_);

        processEspNowDatagram(datagram);
    }
}

void GatewayRelay::processEspNowDatagram(const RxDatagram& datagram) {
    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(datagram.bytes.data(), datagram.length, header, payload, frameError)) return;

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = espNowAssembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    // Discovery ping/pong (ServiceId::Gateway) and trust/pairing handshake traffic
    // (ServiceId::Trust) are this board's own side-channel traffic, not part of the USB host's
    // session -- consume them here instead of relaying onward, and don't count them as "peer
    // seen" relay activity (see handleGatewayServiceFromEspNow/handleTrustFromEspNow). Trust
    // traffic must always flow regardless of the Secure Pairing enforcement check below (it's
    // how trust gets established in the first place).
    MessageEnvelope envelope;
    std::vector<uint8_t> jsonBody;
    CodecError envError;
    const bool decoded = decodeEnvelope(assembled.data(), assembled.size(), envelope, jsonBody, envError);

    if (decoded && envelope.serviceId == ServiceId::Trust) {
        JsonDocument document;
        if (!deserializeJson(document, jsonBody.data(), jsonBody.size())) {
            handleTrustFromEspNow(datagram.mac, document.as<JsonObjectConst>());
        }
        return;
    }

    if (decoded && envelope.serviceId == ServiceId::Gateway) {
        handleGatewayServiceFromEspNow(envelope, jsonBody, datagram.mac);
        return;
    }

    // Secure Pairing enforcement: once enabled, only a sender whose MAC already has a trust
    // record is allowed through to relay. Trust/Gateway traffic is exempt (handled above already,
    // before this point is ever reached).
    if (trustConfig_.securePairingEnabled() && trustConfig_.store().findByMac(datagram.mac) == nullptr) {
        return;  // not a trusted peer: drop rather than relay
    }

    linkStats_.recordPeerSeen(datagram.mac.data(), millis());
    relayToUsb(header, assembled);
}

void GatewayRelay::relayToEspNow(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled) {
    const HopFrameHeader outHeader =
        relayHeaderFor(sourceHeader, CarrierProfileId::EspNowV1, usbToEspNowLinkMessageCounter_++);
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    if (!fragmentIntoHopFrames(assembled.data(), assembled.size(), outHeader, kEspNowMaxDatagramBytes, frames,
                               error)) {
        return;
    }

    // Per-client routing: once Secure Pairing is on and the host has stamped a non-zero routeId
    // (Task 10/11 surface each paired client's routeId to the .NET host so it can do this), send
    // unicast to that specific trusted client instead of broadcasting to everyone in range. Until
    // a host sends a non-zero routeId (or Secure Pairing is off), behavior is unchanged from
    // today's broadcast-everything -- but a routeId that WAS stamped and doesn't resolve (e.g. the
    // host still has a since-forgotten client's routeId cached) must never fall back to broadcast:
    // that would leak a revoked client's traffic to every device in range in the clear, defeating
    // the whole point of enforcing Secure Pairing. Drop it instead.
    const uint8_t* destination = kBroadcastAddress;
    if (trustConfig_.securePairingEnabled() && outHeader.routeId != 0) {
        destination = nullptr;
        for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
            const TrustRecord* record = trustConfig_.store().at(i);
            if (record->routeId == outHeader.routeId) {
                destination = record->mac.data();
                break;
            }
        }
        if (destination == nullptr) return;  // unresolvable routeId: drop, don't broadcast
    }
    for (const auto& frame : frames) esp_now_send(destination, frame.data(), frame.size());
}

void GatewayRelay::relayToUsb(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled) {
    const HopFrameHeader outHeader =
        relayHeaderFor(sourceHeader, CarrierProfileId::StreamStandard, espNowToUsbLinkMessageCounter_++);
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    if (!fragmentIntoHopFrames(assembled.data(), assembled.size(), outHeader, kUsbMaxFrameBytes, frames, error)) {
        return;
    }
    for (const auto& frame : frames) sendUsbFrame(frame);
}

void GatewayRelay::sendUsbFrame(const std::vector<uint8_t>& frame) {
    const std::vector<uint8_t> cobs = cobsEncode(frame.data(), frame.size());
    Serial.write(cobs.data(), cobs.size());
    Serial.write(static_cast<uint8_t>(0x00));
}

namespace {
std::string formatMac(const std::array<uint8_t, 6>& mac) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buffer);
}
}  // namespace

void GatewayRelay::sendGatewayLinkFrame(const char* name, uint32_t echoTs) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = name;
    JsonObject body = wrapper["body"].to<JsonObject>();
    body["role"] = "gateway";
    body["deviceId"] = deviceId_;
    body["firmware"] = ESPBARCODE_VERSION;
    body["ts"] = millis();
    if (echoTs != 0) body["echoTs"] = echoTs;

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Event;
    envelope.serviceId = ServiceId::Gateway;
    envelope.codecId = CodecId::Json;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Event;
    header.profileId = CarrierProfileId::EspNowV1;
    header.linkMessageId = gatewayLocalLinkMessageCounter_++;

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), header, kEspNowMaxDatagramBytes, frames,
                               fragmentError)) {
        return;
    }
    for (const auto& frame : frames) esp_now_send(kBroadcastAddress, frame.data(), frame.size());
}

void GatewayRelay::sendUsbGatewayResponse(const HopFrameHeader& requestHeader, const MessageEnvelope& requestEnvelope,
                                          const std::vector<uint8_t>& bodyBytes) {
    MessageEnvelope envelope;
    envelope.kind = MessageKind::Result;
    // Echoes the request's own serviceId rather than hardcoding Gateway -- this helper is shared
    // by both handleGatewayServiceFromUsb (ServiceId::Gateway) and handleTrustFromUsb
    // (ServiceId::Trust), and the response should carry whichever service the request was for.
    envelope.serviceId = requestEnvelope.serviceId;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = requestEnvelope.controlSessionId;
    envelope.operationId = requestEnvelope.operationId;
    envelope.correlationId = requestEnvelope.operationId;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    const HopFrameHeader outHeader =
        relayHeaderFor(requestHeader, CarrierProfileId::StreamStandard, gatewayLocalLinkMessageCounter_++);

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), outHeader, kUsbMaxFrameBytes, frames, fragmentError)) {
        return;
    }
    for (const auto& frame : frames) sendUsbFrame(frame);
}

bool GatewayRelay::handleGatewayServiceFromUsb(const HopFrameHeader& sourceHeader, const MessageEnvelope& envelope,
                                               const std::vector<uint8_t>& jsonBody) {
    JsonDocument document;
    if (deserializeJson(document, jsonBody.data(), jsonBody.size())) return true;  // malformed: drop, don't relay
    JsonObjectConst wrapper = document.as<JsonObjectConst>();
    const char* name = wrapper["name"] | "";

    JsonDocument responseWrapper;
    responseWrapper["schema"] = "esbg.control/2.0";
    responseWrapper["name"] = name;

    if (std::strcmp(name, "gateway.peers.list") == 0) {
        JsonObject body = responseWrapper["body"].to<JsonObject>();
        JsonArray peers = body["peers"].to<JsonArray>();
        const GatewayStats::Snapshot snap = linkStats_.snapshot(millis());
        for (std::size_t i = 0; i < snap.peerCount; ++i) {
            const GatewayStats::Peer& peer = snap.peers[i];
            JsonObject entry = peers.add<JsonObject>();
            entry["mac"] = formatMac(peer.mac);
            entry["last_seen_ms_ago"] = snap.nowMs - peer.lastSeenMs;
            entry["via_relay"] = peer.everRelayed;
            entry["via_ping"] = peer.everPinged;
            if (peer.everPinged) {
                entry["rtt_ms"] = peer.lastRttMs;
                if (peer.deviceId[0] != '\0') entry["device_id"] = peer.deviceIdCStr();
            }
        }
    } else if (std::strcmp(name, "gateway.ping.now") == 0) {
        JsonObject body = responseWrapper["body"].to<JsonObject>();
        body["ok"] = true;
        lastDiscoveryPingMs_ = millis();
        sendGatewayLinkFrame("gateway.link.ping", 0);
    } else {
        responseWrapper["error"]["code"] = "unknown_command";
        responseWrapper["error"]["message"] = "gateway command not supported this release";
    }

    std::string serialized;
    serializeJson(responseWrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendUsbGatewayResponse(sourceHeader, envelope, bodyBytes);
    return true;
}

bool GatewayRelay::handleGatewayServiceFromEspNow(const MessageEnvelope& envelope,
                                                  const std::vector<uint8_t>& jsonBody,
                                                  const std::array<uint8_t, 6>& fromMac) {
    (void)envelope;
    JsonDocument document;
    if (deserializeJson(document, jsonBody.data(), jsonBody.size())) return true;  // malformed: drop, don't relay
    JsonObjectConst wrapper = document.as<JsonObjectConst>();
    const char* name = wrapper["name"] | "";
    JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();
    const char* role = body["role"] | "";
    if (std::strcmp(role, "client") != 0) return true;  // ignore chatter from other gateways

    const uint32_t now = millis();
    if (std::strcmp(name, "gateway.link.pong") == 0) {
        // Reply to our own periodic/manual probe.
        const uint32_t echoTs = body["echoTs"] | 0;
        const uint32_t rttMs = (echoTs != 0 && now >= echoTs) ? (now - echoTs) : 0;
        const char* deviceId = body["deviceId"] | "";
        linkStats_.recordDiscoveryPong(fromMac.data(), now, rttMs, deviceId);
    } else if (std::strcmp(name, "gateway.link.ping") == 0) {
        // A prospective client is probing for a gateway before we've broadcast one of our
        // own -- reply immediately rather than making it wait for our next periodic ping.
        const uint32_t ts = body["ts"] | 0;
        sendGatewayLinkFrame("gateway.link.pong", ts);
    }
    return true;
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
// it to whatever's polling (trust.pair.status over USB) -- see beginPairing()/handleTrustFromEspNow's
// "trust.pair.begin" branch, which reset() a stale terminal result at the start of the *next*
// attempt rather than the moment it's reached.
bool isTerminalPairingState(TrustPairingState state) {
    return state == TrustPairingState::Committed || state == TrustPairingState::Cancelled;
}

bool isPairingInFlight(TrustPairingState state) {
    return state == TrustPairingState::AwaitingPeerHello || state == TrustPairingState::AwaitingApproval ||
           state == TrustPairingState::AwaitingPeerConfirm;
}
}  // namespace

void GatewayRelay::sendTrustFrameTo(const std::array<uint8_t, 6>& toMac, const char* name, JsonObject body) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = name;
    wrapper["body"].set(body);

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Event;
    envelope.serviceId = ServiceId::Trust;
    envelope.codecId = CodecId::Json;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Event;
    header.profileId = CarrierProfileId::EspNowV1;
    header.linkMessageId = gatewayLocalLinkMessageCounter_++;

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), header, kEspNowMaxDatagramBytes, frames,
                              fragmentError)) {
        return;
    }
    for (const auto& frame : frames) esp_now_send(toMac.data(), frame.data(), frame.size());
}

bool GatewayRelay::ensureUnencryptedPeer(const std::array<uint8_t, 6>& mac) {
    // An existing entry is only reusable if it is actually unencrypted -- same contract and same
    // rationale as EspNowEndpoint::ensureUnencryptedPeer (Task 6). A leftover encrypt=true entry
    // would encrypt this handshake's plaintext hello under a stale LMK the peer can no longer
    // decrypt, killing the handshake silently. Tear it down and re-add it unencrypted;
    // finalizePairingCommit re-upgrades it once the attempt commits.
    esp_now_peer_info_t existing{};
    if (esp_now_get_peer(mac.data(), &existing) == ESP_OK) {
        if (!existing.encrypt) return true;
        esp_now_del_peer(mac.data());
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;  // 0 = use the current channel, already fixed by EspNowEndpoint::begin()
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool GatewayRelay::upgradeToEncryptedPeer(const std::array<uint8_t, 6>& mac, const std::array<uint8_t, 16>& lmk) {
    esp_now_del_peer(mac.data());
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac.data(), 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    memcpy(peer.lmk, lmk.data(), lmk.size());
    return esp_now_add_peer(&peer) == ESP_OK;
}

void GatewayRelay::evictUntrustedPeer(const std::array<uint8_t, 6>& mac) {
    if (trustConfig_.store().findByMac(mac) == nullptr) {
        esp_now_del_peer(mac.data());
    }
    // else: this mac has an established trust record -- some *other* successful pairing (past or
    // present) put this peer-table entry there, so a failed/cancelled/timed-out attempt touching
    // the same mac must not tear down a working encrypted connection.
}

void GatewayRelay::finalizePairingCommit(const std::array<uint8_t, 6>& mac) {
    const bool alreadyTrusted = trustConfig_.store().findByMac(mac) != nullptr;
    TrustRecord record;
    record.staticPublicKey = trustPairing_.peerHello().staticPublicKey;
    record.mac = mac;
    record.peerRole = TrustRole::Client;  // GatewayRelay always pairs with a client
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
        if (shouldSend) {
            JsonDocument body;
            sendTrustFrameTo(mac, "trust.pair.cancel", body.to<JsonObject>());
        }
        evictUntrustedPeer(mac);
        return;
    }
    upgradeToEncryptedPeer(mac, trustPairing_.derivedKeys().lmk);
    // Deliberately not reset() here -- state() stays Committed so pairingStatus() can report
    // success; the next beginPairing()/incoming trust.pair.begin resets it (see there).
}

bool GatewayRelay::beginPairing(const std::array<uint8_t, 6>& targetMac) {
    // A stale terminal result from a *previous* attempt must not block a new one -- see
    // pairingStatus()'s comment on why Committed/Cancelled linger instead of auto-resetting.
    if (isTerminalPairingState(trustPairing_.state())) trustPairing_.reset();
    if (trustPairing_.state() != TrustPairingState::Idle) return false;
    if (!ensureUnencryptedPeer(targetMac)) return false;

    TrustHelloMessage hello;
    if (!trustPairing_.beginAsInitiator(trustConfig_.identity(), millis(), hello)) return false;
    pairingTargetMac_ = targetMac;
    pairingIsInitiator_ = true;

    JsonDocument body;
    encodeHelloJson(hello, body.to<JsonObject>());
    sendTrustFrameTo(targetMac, "trust.pair.begin", body.as<JsonObject>());
    return true;
}

void GatewayRelay::confirmPairing() {
    TrustSignature signature{};
    if (!trustPairing_.confirmLocally(millis(), signature)) return;

    JsonDocument body;
    body["confirmSignature"] = bytesToBase64(std::vector<uint8_t>(signature.begin(), signature.end()));
    sendTrustFrameTo(pairingTargetMac_, "trust.pair.confirm", body.as<JsonObject>());

    if (trustPairing_.state() == TrustPairingState::Committed) {
        finalizePairingCommit(pairingTargetMac_);
    }
}

void GatewayRelay::denyPairing() {
    bool shouldSend = false;
    trustPairing_.cancel(shouldSend);
    if (shouldSend) {
        JsonDocument body;
        sendTrustFrameTo(pairingTargetMac_, "trust.pair.cancel", body.to<JsonObject>());
    }
    evictUntrustedPeer(pairingTargetMac_);
    // Deliberately not reset() here -- state() stays Cancelled so pairingStatus() can report it;
    // the next beginPairing()/incoming trust.pair.begin resets it (see there).
}

GatewayRelay::TrustPairingUiStatus GatewayRelay::pairingStatus() const {
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
            // currentOutcome() only yields a fingerprint/code in AwaitingApproval -- the one state
            // a local tap is still outstanding in -- so a trusted-peer reconnect (which
            // auto-confirms straight through to AwaitingPeerConfirm) leaves both empty and stays
            // silent on the Trust screen. Same contract as EspNowEndpoint's (Task 6).
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

std::vector<std::string> GatewayRelay::fingerprintList() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
        const TrustRecord* record = trustConfig_.store().at(i);
        TrustHash hash{};
        trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
        out.push_back(trustFingerprint(hash));
    }
    return out;
}

std::vector<std::array<uint8_t, 6>> GatewayRelay::macList() const {
    std::vector<std::array<uint8_t, 6>> out;
    for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) out.push_back(trustConfig_.store().at(i)->mac);
    return out;
}

bool GatewayRelay::forgetByFingerprint(const std::string& fingerprint) {
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

        // Drop the ESP-NOW peer-table entry along with the record -- same contract and rationale
        // as EspNowEndpoint::forgetByFingerprint (Task 6): a surviving encrypt=true entry with a
        // revoked LMK would silently break the spec's "Forget forces re-pairing on next contact"
        // guarantee. Failure is ignored; "no such peer" is a normal outcome here.
        esp_now_del_peer(mac.data());
        return true;
    }
    return false;
}

void GatewayRelay::handleTrustFromEspNow(const std::array<uint8_t, 6>& fromMac, JsonObjectConst wrapper) {
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
        if (trustPairing_.state() == TrustPairingState::AwaitingPeerHello && pairingTargetMac_ != fromMac) {
            return;
        }

        // A peer we already trust: verify the incoming hello against the *pinned* key on file,
        // not just its own self-consistency signature -- a MAC-spoofing attacker presenting a
        // different static key must be rejected outright rather than "verified against itself"
        // (see verifyTrustHello's doc comment).
        const TrustRecord* existing = trustConfig_.store().findByMac(fromMac);
        if (existing != nullptr && !verifyTrustHello(trustCrypto_, existing->staticPublicKey, peerHello)) {
            return;
        }

        // Was this device already mid-handshake as the initiator, specifically awaiting this
        // peer's reply? If so, this hello must be folded into *that* attempt --
        // trustPairing_.onPeerHello() below, called while in AwaitingPeerHello, does exactly this
        // using the ephemeral we already generated in beginPairing(). Generating a brand new
        // ephemeral for every incoming hello instead (rather than reusing that one) would make
        // the two sides derive a different session key on every round trip and never converge --
        // this check is what keeps a reconnect handshake to a single round trip.
        const bool wasAwaitingOurOwnHello = pairingIsInitiator_ && pairingTargetMac_ == fromMac &&
                                            trustPairing_.state() == TrustPairingState::AwaitingPeerHello;

        // Captured *before* calling onPeerHello: it can return false for two different reasons,
        // and only one of them means an attempt was actually abandoned. From Idle (about to
        // become a fresh responder) or AwaitingPeerHello (we're the initiator awaiting exactly
        // this peer's reply -- guaranteed above), a signature failure makes onPeerHello reset()
        // internally, so evicting the peer entry below is correct. From any other state, a
        // *different* attempt is already in flight and onPeerHello's fallback `return false`
        // deliberately leaves that other attempt's state untouched -- evicting in that case would
        // delete a real, still-needed peer entry for whichever peer we're actually mid-handshake
        // with.
        const bool preCallStateResetsOnFailure = trustPairing_.state() == TrustPairingState::Idle ||
                                                  trustPairing_.state() == TrustPairingState::AwaitingPeerHello;

        TrustHelloMessage reply;
        bool hasReply = false;
        if (!trustPairing_.onPeerHello(peerHello, trustConfig_.identity(), millis(), reply, hasReply)) {
            if (preCallStateResetsOnFailure) evictUntrustedPeer(fromMac);
            return;
        }

        // Only now -- after onPeerHello's self-consistency signature check has actually passed --
        // do we register a peer-table entry for this sender. Registering unconditionally for
        // every incoming trust.pair.begin, before validating it, would let a flood of junk hellos
        // from distinct MACs exhaust ESP-NOW's small hardware peer-table ceiling; since only one
        // attempt can be in flight at a time (trustPairing_ is a single instance), at most one
        // such entry is ever at risk, and it's evicted on timeout/failure above.
        if (!ensureUnencryptedPeer(fromMac)) {
            bool ignoredShouldSend = false;
            trustPairing_.cancel(ignoredShouldSend);  // can't reply without a peer-table slot
            return;
        }

        if (!wasAwaitingOurOwnHello) {
            // We're the responder to this attempt (fresh or reconnect) -- remember who, for
            // confirmPairing()/denyPairing() and the auto-confirm below.
            pairingTargetMac_ = fromMac;
            pairingIsInitiator_ = false;
        }
        if (hasReply) {
            JsonDocument replyBody;
            encodeHelloJson(reply, replyBody.to<JsonObject>());
            sendTrustFrameTo(fromMac, "trust.pair.begin", replyBody.as<JsonObject>());
        }

        // Reconnect (design spec §1 "Reconnect"): the peer is already trusted by this device, so
        // no human approval is needed on this side -- confirm immediately instead of waiting for
        // a UI tap. First-time pairing (existing == nullptr) still waits for confirmPairing().
        if (existing != nullptr) confirmPairing();
        return;
    }

    if (std::strcmp(name, "trust.pair.confirm") == 0) {
        if (fromMac != pairingTargetMac_) return;  // not the peer we're mid-handshake with

        std::vector<uint8_t> signatureBytes;
        if (!bytesFromBase64(body["confirmSignature"] | "", signatureBytes) ||
            signatureBytes.size() != kTrustSignatureBytes) {
            return;
        }
        TrustSignature signature{};
        std::memcpy(signature.data(), signatureBytes.data(), signature.size());
        if (!trustPairing_.onPeerConfirm(signature, millis())) {
            // Bad signature: onPeerConfirm() already reset() internally (state -> Idle). `fromMac`
            // is confirmed equal to pairingTargetMac_ by the guard above, so this attempt's own
            // peer entry -- not some unrelated one -- is what needs evicting here. Without this,
            // an attacker could register a peer via a self-consistent junk hello, then force a
            // reset-without-eviction by sending garbage as trust.pair.confirm, and repeat with new
            // MACs to exhaust ESP-NOW's small peer-table ceiling.
            evictUntrustedPeer(fromMac);
            return;
        }
        if (trustPairing_.state() == TrustPairingState::Committed) {
            finalizePairingCommit(fromMac);
        }
        return;
    }

    if (std::strcmp(name, "trust.pair.cancel") == 0) {
        // Only the peer we're actually mid-handshake with may cancel it, and only while an
        // attempt is genuinely in flight -- otherwise any nearby sender could tear down an
        // unrelated in-progress pairing attempt (or a just-completed one still waiting for
        // pairingStatus() to report it) with a stray message.
        if (fromMac == pairingTargetMac_ && isPairingInFlight(trustPairing_.state())) {
            bool ignoredShouldSend = false;
            trustPairing_.cancel(ignoredShouldSend);  // -> Cancelled; we're reacting to their
                                                       // cancel, so never send one back
            evictUntrustedPeer(fromMac);
        }
    }
}

namespace {
// Duplicated locally rather than shared with TrustConfigStore.cpp -- this file already
// duplicates formatMac() the same way rather than sharing it with EspNowEndpoint.cpp.
bool macFromString(const std::string& text, std::array<uint8_t, 6>& out) {
    unsigned values[6];
    if (std::sscanf(text.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &values[0], &values[1], &values[2], &values[3],
                    &values[4], &values[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) out[static_cast<std::size_t>(i)] = static_cast<uint8_t>(values[i]);
    return true;
}
}  // namespace

bool GatewayRelay::handleTrustFromUsb(const HopFrameHeader& sourceHeader, const MessageEnvelope& envelope,
                                      JsonObjectConst wrapper) {
    const char* name = wrapper["name"] | "";

    JsonDocument responseWrapper;
    responseWrapper["schema"] = "esbg.control/2.0";
    responseWrapper["name"] = name;

    if (std::strcmp(name, "trust.controllers.list") == 0) {
        JsonArray peers = responseWrapper["body"]["peers"].to<JsonArray>();
        for (std::size_t i = 0; i < trustConfig_.store().size(); ++i) {
            const TrustRecord* record = trustConfig_.store().at(i);
            JsonObject entry = peers.add<JsonObject>();
            TrustHash hash{};
            trustCrypto_.sha256(record->staticPublicKey.data(), record->staticPublicKey.size(), hash);
            entry["fingerprint"] = trustFingerprint(hash);
            entry["mac"] = formatMac(record->mac);
            entry["route_id"] = record->routeId;
            entry["paired_at_ms"] = record->pairedAtMs;
            entry["label"] = record->label;
        }
    } else if (std::strcmp(name, "trust.controller.forget") == 0) {
        JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();
        const bool found = forgetByFingerprint(body["fingerprint"] | "");
        responseWrapper["body"]["ok"] = found;
        if (!found) {
            responseWrapper["error"]["code"] = "not_found";
            responseWrapper["error"]["message"] = "no trust record with that fingerprint";
        }
    } else if (std::strcmp(name, "trust.pair.begin") == 0) {
        JsonObjectConst body = wrapper["body"].as<JsonObjectConst>();
        std::array<uint8_t, 6> targetMac{};
        const bool parsedMac = macFromString(body["mac"] | "", targetMac);
        responseWrapper["body"]["ok"] = parsedMac && beginPairing(targetMac);
        if (!parsedMac) {
            responseWrapper["error"]["code"] = "invalid_mac";
            responseWrapper["error"]["message"] = "mac must be formatted AA:BB:CC:DD:EE:FF";
        }
    } else if (std::strcmp(name, "trust.pair.cancel") == 0) {
        // Only cancel something that's actually in flight. Calling denyPairing() unconditionally
        // drove an Idle session to Cancelled, so a stray host cancel made the next
        // trust.pair.status report "cancelled" for an attempt that never existed -- the same
        // guard the ESP-NOW-side cancel branch already applies.
        const bool inFlight = isPairingInFlight(trustPairing_.state());
        if (inFlight) denyPairing();
        responseWrapper["body"]["ok"] = inFlight;
    } else if (std::strcmp(name, "trust.pair.status") == 0) {
        const TrustPairingUiStatus status = pairingStatus();
        JsonObject responseBody = responseWrapper["body"].to<JsonObject>();
        switch (status.state) {
            case TrustPairingUiState::Idle: responseBody["state"] = "idle"; break;
            case TrustPairingUiState::Discovering: responseBody["state"] = "discovering"; break;
            case TrustPairingUiState::AwaitingApproval: responseBody["state"] = "awaiting_approval"; break;
            case TrustPairingUiState::Committed: responseBody["state"] = "committed"; break;
            case TrustPairingUiState::Cancelled: responseBody["state"] = "cancelled"; break;
        }
        // Emitted unconditionally while awaiting approval, including a legitimately-derived
        // numeric code of 0 (1-in-a-million, but it would otherwise render as a blank code in the
        // Blazor Trust card and leave the operator with nothing to compare). Outside that state
        // there is genuinely nothing to report, so the fields stay absent.
        if (status.state == TrustPairingUiState::AwaitingApproval) {
            responseBody["fingerprint"] = status.peerFingerprint;
            responseBody["numeric_code"] = status.numericCode;
        }
    } else {
        responseWrapper["error"]["code"] = "unknown_command";
        responseWrapper["error"]["message"] = "trust command not supported this release";
    }

    std::string serialized;
    serializeJson(responseWrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendUsbGatewayResponse(sourceHeader, envelope, bodyBytes);
    return true;
}

GatewayRelay::Stats GatewayRelay::stats() const {
    Stats out;
    out.linkStats = linkStats_.snapshot(millis());
    // Counters are pre-incremented (post-increment on send, starting at 1), so the number of
    // messages actually sent so far is one less than the next id they'd be stamped with.
    out.usbToEspNowMessageCount = usbToEspNowLinkMessageCounter_ - 1;
    out.espNowToUsbMessageCount = espNowToUsbLinkMessageCounter_ - 1;
    return out;
}

}  // namespace esplink
