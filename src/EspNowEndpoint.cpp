#include "EspNowEndpoint.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>

#include "Fragmenter.h"
#include "JsonCommandCodec.h"

namespace esplink {

namespace {
// ESP-NOW requires both peers to share a fixed radio channel before pairing; there is no
// channel-negotiation handshake in this compatibility profile. Broadcasting on the
// broadcast address means this endpoint accepts commands from *any* peer on this channel —
// acceptable for this bench-validated compatibility profile, where trust/pairing (design
// plan §7.7) is explicitly out of scope until a later PR (docs/PROTOCOL_V2.md §10).
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
    return true;
}

void EspNowEndpoint::enqueueReceived(const uint8_t* mac, const uint8_t* data, std::size_t length) {
    (void)mac;  // this endpoint accepts any sender this session; see kBroadcastAddress note above
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

    processMessage(envelope, body);
}

void EspNowEndpoint::processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body) {
    if (envelope.kind != MessageKind::Command) return;  // this endpoint only accepts commands from a controller

    JsonDocument document;
    if (deserializeJson(document, body.data(), body.size())) return;
    JsonObjectConst wrapper = document.as<JsonObjectConst>();
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

// Same v2-name -> v1-name subset SerialCobsEndpoint maps (docs/PROTOCOL_V2.md §7) — the
// ESP-NOW endpoint drives the identical ControlProtocolEngine handlers, just over a
// smaller-frame carrier.
const char* EspNowEndpoint::mapV2Name(const std::string& name) {
    if (name == "system.hello" || name == "system.ping") return "hello";
    if (name == "barcode.generate") return "generate";
    if (name == "barcode.close") return "close";
    if (name == "device.backlight.set") return "backlight";
    return nullptr;
}

void EspNowEndpoint::sendEnvelope(MessageKind kind, ServiceId serviceId, const std::vector<uint8_t>& bodyBytes,
                                  uint64_t correlationId) {
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
        esp_now_send(kBroadcastAddress, frame.data(), frame.size());
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
    sendEnvelope(MessageKind::Result, ServiceId::System, bodyBytes, currentRequestOperationId_);
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
    sendEnvelope(MessageKind::Error, ServiceId::System, bodyBytes, currentRequestOperationId_);
}

}  // namespace esplink
