#include "SerialLegacyEndpoint.h"

#include <cctype>

#include "JsonCommandCodec.h"
#include "app_config.h"

namespace esplink {

SerialLegacyEndpoint::SerialLegacyEndpoint(ControlProtocolEngine& engine, ControlSession& session,
                                           IDeviceControl& deviceControl, const IBarcodeDevice& device)
    : engine_(engine), session_(session), deviceControl_(deviceControl), device_(device) {}

void SerialLegacyEndpoint::begin() {
    line_.reserve(512);
    JsonDocument event;
    event["event"] = "ready";
    event["device"] = app_config::kDeviceName;
    event["protocol"] = app_config::kProtocolVersion;
    event["firmware"] = ESPBARCODE_VERSION;
    serializeJson(event, Serial);
    Serial.write('\n');
}

void SerialLegacyEndpoint::loop() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c == '\n') {
            if (discardingLine_) {
                discardingLine_ = false;
                line_.clear();
                JsonDocument response;
                response["ok"] = false;
                response["error"]["code"] = "line_too_long";
                response["error"]["message"] = "request exceeded serial line limit";
                serializeJson(response, Serial);
                Serial.write('\n');
            } else if (!line_.empty()) {
                processLine(line_);
                line_.clear();
            }
            continue;
        }
        if (discardingLine_) continue;
        if (line_.size() >= app_config::kSerialLineLimit) {
            discardingLine_ = true;
            line_.clear();
            continue;
        }
        line_.push_back(c);
    }
}

void SerialLegacyEndpoint::processLine(const std::string& line) {
    JsonDocument document;
    const DeserializationError parseError = deserializeJson(document, line);
    if (parseError) {
        JsonDocument response;
        response["ok"] = false;
        response["error"]["code"] = "invalid_json";
        response["error"]["message"] = parseError.c_str();
        serializeJson(response, Serial);
        Serial.write('\n');
        return;
    }
    if (!document.is<JsonObject>()) {
        JsonDocument response;
        response["ok"] = false;
        response["error"]["code"] = "invalid_request";
        response["error"]["message"] = "request must be a JSON object";
        serializeJson(response, Serial);
        Serial.write('\n');
        return;
    }

    JsonObjectConst request = document.as<JsonObjectConst>();
    const char* commandText = request["cmd"] | "";
    std::string commandName(commandText);
    for (char& ch : commandName) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if (commandName.empty()) {
        JsonDocument response;
        response["ok"] = false;
        if (request["id"] && !request["id"].isNull()) response["id"].set(request["id"]);
        response["error"]["code"] = "missing_command";
        response["error"]["message"] = "cmd is required";
        serializeJson(response, Serial);
        Serial.write('\n');
        return;
    }

    if (commandName == "upgrade") {
        JsonDocument response;
        response["ok"] = true;
        response["cmd"] = "upgrade";
        response["message"] = "switching to EspLink v2 COBS framing";
        if (request["id"] && !request["id"].isNull()) response["id"].set(request["id"]);
        serializeJson(response, Serial);
        Serial.write('\n');
        Serial.flush();
        upgradeRequested_ = true;
        return;
    }

    Command command;
    std::string errorCode, errorMessage;
    if (!JsonCommandCodec::decode(commandName, request, device_, command, errorCode, errorMessage)) {
        if (errorCode.empty()) {
            errorCode = "unknown_command";
            errorMessage = "unsupported command";
        }
        currentRequest_ = request;
        sendError(ProtocolError{commandName, errorCode, errorMessage});
        currentRequest_ = JsonObjectConst();
        return;
    }

    currentRequest_ = request;
    engine_.handle(session_, command, OperationId{nextOperationId_++}, "usb-uart-ndjson", *this);
    currentRequest_ = JsonObjectConst();
}

void SerialLegacyEndpoint::send(const Response& response) {
    JsonDocument out;
    JsonCommandCodec::encode(response, currentRequest_, out);
    serializeJson(out, Serial);
    Serial.write('\n');
}

void SerialLegacyEndpoint::sendError(const ProtocolError& error) {
    JsonDocument out;
    JsonCommandCodec::encodeError(error, currentRequest_, out);
    serializeJson(out, Serial);
    Serial.write('\n');
}

}  // namespace esplink
