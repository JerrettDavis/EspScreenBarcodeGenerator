#include "WifiDirectTcpEndpoint.h"

#include "Fragmenter.h"
#include "JsonCommandCodec.h"

namespace esplink {

WifiDirectTcpEndpoint::WifiDirectTcpEndpoint(ControlProtocolEngine& engine, ControlSession& session,
                                             const IBarcodeDevice& device)
    : engine_(engine), session_(session), device_(device) {}

void WifiDirectTcpEndpoint::begin() {
    credentials_ = provisioning_.load();
    if (credentials_.valid) {
        state_ = State::Idle;  // loop() will pick this up and start connecting immediately
    }
}

bool WifiDirectTcpEndpoint::configure(const std::string& ssid, const std::string& passphrase, uint16_t port,
                                      std::string& error) {
    WifiDirectCredentials next;
    next.ssid = ssid;
    next.passphrase = passphrase;
    next.port = port;
    next.valid = true;

    if (!provisioning_.save(next, error)) return false;

    resetConnection(State::Idle);
    credentials_ = next;
    lastConnectAttemptMs_ = 0;  // let loop() attempt the new credentials on its very next tick
    Serial.printf("{\"event\":\"wifidirect_configured\",\"ssid\":\"%s\",\"port\":%u}\n", ssid.c_str(), port);
    return true;
}

void WifiDirectTcpEndpoint::resetConnection(State nextState) {
    if (client_.connected()) client_.stop();
    frameParser_.reset();
    state_ = nextState;
    stateEnteredAtMs_ = millis();
}

void WifiDirectTcpEndpoint::loop() {
    if (!credentials_.valid) return;

    const uint32_t now = millis();

    switch (state_) {
        case State::Idle: {
            if (now - lastConnectAttemptMs_ < kReconnectBackoffMs && lastConnectAttemptMs_ != 0) return;
            lastConnectAttemptMs_ = now;
            WiFi.mode(WIFI_STA);
            WiFi.begin(credentials_.ssid.c_str(), credentials_.passphrase.c_str());
            Serial.printf("{\"event\":\"wifidirect_wifi_connecting\",\"ssid\":\"%s\"}\n", credentials_.ssid.c_str());
            state_ = State::WifiConnecting;
            stateEnteredAtMs_ = now;
            break;
        }
        case State::WifiConnecting: {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("{\"event\":\"wifidirect_wifi_connected\",\"ip\":\"%s\",\"gateway\":\"%s\"}\n",
                              WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
                state_ = State::TcpConnecting;
                stateEnteredAtMs_ = now;
                break;
            }
            if (now - stateEnteredAtMs_ > kWifiConnectTimeoutMs) {
                Serial.printf("{\"event\":\"wifidirect_wifi_timeout\",\"status\":%d}\n", static_cast<int>(WiFi.status()));
                WiFi.disconnect(true);
                resetConnection(State::Idle);
            }
            break;
        }
        case State::TcpConnecting: {
            // WiFiClient::connect() is blocking; on a direct single-hop link to the group
            // owner this is bounded to at most a few hundred milliseconds in practice. A
            // fully non-blocking connect state machine is a documented follow-up
            // (docs/PROTOCOL_V2.md §12).
            if (client_.connect(WiFi.gatewayIP(), credentials_.port)) {
                Serial.printf("{\"event\":\"wifidirect_tcp_connected\"}\n");
                state_ = State::Connected;
                stateEnteredAtMs_ = now;
                break;
            }
            if (now - stateEnteredAtMs_ > kTcpConnectTimeoutMs) {
                Serial.printf("{\"event\":\"wifidirect_tcp_timeout\",\"gateway\":\"%s\",\"port\":%u}\n",
                              WiFi.gatewayIP().toString().c_str(), credentials_.port);
                resetConnection(State::Idle);
            }
            break;
        }
        case State::Connected: {
            if (!client_.connected()) {
                resetConnection(State::Idle);
                break;
            }
            pumpReceive();
            break;
        }
    }
}

void WifiDirectTcpEndpoint::pumpReceive() {
    uint8_t buffer[512];
    while (client_.available() > 0) {
        const int read = client_.read(buffer, sizeof(buffer));
        if (read <= 0) break;

        std::vector<std::vector<uint8_t>> frames;
        if (!frameParser_.feed(buffer, static_cast<std::size_t>(read), frames)) {
            resetConnection(State::Idle);  // oversized/zero declared length: not recoverable in-stream
            return;
        }

        for (const auto& frame : frames) {
            HopFrameHeader header;
            std::vector<uint8_t> payload;
            CodecError frameError;
            if (!decodeHopFrame(frame.data(), frame.size(), header, payload, frameError)) continue;

            std::vector<uint8_t> assembled;
            const AssemblyOutcome outcome = assembler_.addFragment(header, payload, assembled);
            if (outcome != AssemblyOutcome::Complete) continue;

            MessageEnvelope envelope;
            std::vector<uint8_t> body;
            CodecError envelopeError;
            if (!decodeEnvelope(assembled.data(), assembled.size(), envelope, body, envelopeError)) continue;

            processMessage(envelope, body);
        }
    }
}

void WifiDirectTcpEndpoint::processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body) {
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

    engine_.handle(session_, command, OperationId{envelope.operationId}, "wifi-direct-tcp-v1", *this);
}

// Same v2-name -> v1-name subset every other v2 endpoint maps (docs/PROTOCOL_V2.md §7).
const char* WifiDirectTcpEndpoint::mapV2Name(const std::string& name) {
    if (name == "system.hello" || name == "system.ping") return "hello";
    if (name == "barcode.generate") return "generate";
    if (name == "barcode.close") return "close";
    if (name == "device.backlight.set") return "backlight";
    return nullptr;
}

void WifiDirectTcpEndpoint::sendEnvelope(MessageKind kind, ServiceId serviceId, const std::vector<uint8_t>& bodyBytes,
                                         uint64_t correlationId) {
    if (state_ != State::Connected) return;

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
    header.profileId = CarrierProfileId::TcpStandard;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), header, kMaxFrameBytes, frames, fragmentError)) {
        return;
    }

    for (const auto& frame : frames) {
        const std::vector<uint8_t> framed = encodeLengthPrefixedFrame(frame);
        client_.write(framed.data(), framed.size());
    }
}

void WifiDirectTcpEndpoint::send(const Response& response) {
    const std::string responseName = (currentRequestName_ == "system.hello") ? "system.welcome" : currentRequestName_;

    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = responseName;
    JsonObject body = wrapper["body"].to<JsonObject>();
    if (responseName == "system.welcome") {
        body["deviceId"] = "esbg-wifi-direct-tcp-v1";
        body["firmware"] = ESPBARCODE_VERSION;
        body["selectedVersion"] = "2.0";
        body["controlSessionId"] = session_.id().value;
        body["carrier"]["profile"] = "tcp-standard";
        body["carrier"]["maxFrameBytes"] = static_cast<uint32_t>(kMaxFrameBytes);
    } else {
        JsonCommandCodec::encodeBody(response, body);
    }

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelope(MessageKind::Result, ServiceId::System, bodyBytes, currentRequestOperationId_);
}

void WifiDirectTcpEndpoint::sendError(const ProtocolError& error) {
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
