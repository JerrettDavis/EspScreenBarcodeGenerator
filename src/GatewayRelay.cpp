#include "GatewayRelay.h"

#include <ArduinoJson.h>
#include <esp_now.h>

#include <cstdio>
#include <cstring>

#include "Cobs.h"
#include "Fragmenter.h"
#include "GatewayBridge.h"

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
    // nothing would understand them.
    MessageEnvelope envelope;
    std::vector<uint8_t> jsonBody;
    CodecError envError;
    if (decodeEnvelope(assembled.data(), assembled.size(), envelope, jsonBody, envError) &&
        envelope.serviceId == ServiceId::Gateway) {
        if (handleGatewayServiceFromUsb(header, envelope, jsonBody)) return;
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

    // Discovery ping/pong (ServiceId::Gateway) is this board's own side-channel traffic, not
    // part of the USB host's session -- consume it here instead of relaying it onward, and
    // don't count it as "peer seen" relay activity (see handleGatewayServiceFromEspNow).
    MessageEnvelope envelope;
    std::vector<uint8_t> jsonBody;
    CodecError envError;
    if (decodeEnvelope(assembled.data(), assembled.size(), envelope, jsonBody, envError) &&
        envelope.serviceId == ServiceId::Gateway) {
        handleGatewayServiceFromEspNow(envelope, jsonBody, datagram.mac);
        return;
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
    for (const auto& frame : frames) esp_now_send(kBroadcastAddress, frame.data(), frame.size());
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
    envelope.serviceId = ServiceId::Gateway;
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
