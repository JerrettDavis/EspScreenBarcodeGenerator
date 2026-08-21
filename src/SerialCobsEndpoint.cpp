#include "SerialCobsEndpoint.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "JsonCommandCodec.h"

namespace esplink {

namespace {
// This session's v2-name -> v1-name subset (see Task 9's "Deliberate scope cut").
const char* mapV2Name(const std::string& name) {
    if (name == "system.hello" || name == "system.ping") return "hello";
    if (name == "barcode.generate") return "generate";
    if (name == "barcode.close") return "close";
    if (name == "device.backlight.set") return "backlight";
    return nullptr;
}
}  // namespace

SerialCobsEndpoint::SerialCobsEndpoint(ControlProtocolEngine& engine, ControlSession& session,
                                       const IBarcodeDevice& device)
    : engine_(engine), session_(session), device_(device) {}

void SerialCobsEndpoint::loop() {
    while (Serial.available() > 0) {
        const uint8_t b = static_cast<uint8_t>(Serial.read());
        if (b == 0x00) {
            if (!rxBlock_.empty()) processCobsBlock(rxBlock_);
            rxBlock_.clear();
            continue;
        }
        if (rxBlock_.size() < 2048) rxBlock_.push_back(b);  // bounded; oversized blocks are dropped at the delimiter
    }
}

void SerialCobsEndpoint::processCobsBlock(const std::vector<uint8_t>& block) {
    std::vector<uint8_t> raw;
    if (!cobsDecode(block.data(), block.size(), raw)) return;  // malformed block: drop and resync on the next 0x00

    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(raw.data(), raw.size(), header, payload, frameError)) return;  // CRC/format failure: drop

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = assembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    MessageEnvelope envelope;
    std::vector<uint8_t> body;
    CodecError envelopeError;
    if (!decodeEnvelope(assembled.data(), assembled.size(), envelope, body, envelopeError)) return;

    processMessage(envelope, body);
}

void SerialCobsEndpoint::processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body) {
    if (envelope.kind != MessageKind::Command) return;  // this endpoint only accepts commands from the host

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

    engine_.handle(session_, command, OperationId{envelope.operationId}, "usb-cobs-v2", *this);
}

void SerialCobsEndpoint::send(const Response& response) {
    // system.hello gets a distinct response name/body per the design doc §8.10 — every
    // other mapped command echoes its own request name.
    const std::string responseName = (currentRequestName_ == "system.hello") ? "system.welcome" : currentRequestName_;

    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = responseName;
    JsonObject body = wrapper["body"].to<JsonObject>();
    if (responseName == "system.welcome") {
        body["deviceId"] = "esbg-usb-v2";
        body["firmware"] = ESPBARCODE_VERSION;
        body["selectedVersion"] = "2.0";
        body["controlSessionId"] = session_.id().value;
        body["carrier"]["profile"] = "stream-standard";
        body["carrier"]["maxFrameBytes"] = 4096;
    } else {
        JsonCommandCodec::encodeBody(response, body);
    }

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Result;
    envelope.serviceId = ServiceId::System;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = session_.id().value;
    envelope.operationId = nextResponseOperationId_++;
    envelope.correlationId = currentRequestOperationId_;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    // Single-fragment response only this session — see Task 9's scope note; a response
    // body larger than one hop frame's payload budget is dropped rather than fragmented.
    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::StreamStandard;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;
    header.fragmentIndex = 0;
    header.fragmentCount = 1;

    std::vector<uint8_t> frame;
    CodecError frameError;
    if (!encodeHopFrame(header, message.data(), message.size(), frame, frameError)) return;

    const std::vector<uint8_t> cobs = cobsEncode(frame.data(), frame.size());
    Serial.write(cobs.data(), cobs.size());
    Serial.write(static_cast<uint8_t>(0x00));
}

void SerialCobsEndpoint::sendError(const ProtocolError& error) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = currentRequestName_;
    wrapper["error"]["code"] = error.code;
    wrapper["error"]["message"] = error.message;

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());

    MessageEnvelope envelope;
    envelope.kind = MessageKind::Error;
    envelope.serviceId = ServiceId::System;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = session_.id().value;
    envelope.operationId = nextResponseOperationId_++;
    envelope.correlationId = currentRequestOperationId_;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::StreamStandard;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;
    header.fragmentIndex = 0;
    header.fragmentCount = 1;

    std::vector<uint8_t> frame;
    CodecError frameError;
    if (!encodeHopFrame(header, message.data(), message.size(), frame, frameError)) return;

    const std::vector<uint8_t> cobs = cobsEncode(frame.data(), frame.size());
    Serial.write(cobs.data(), cobs.size());
    Serial.write(static_cast<uint8_t>(0x00));
}

}  // namespace esplink
