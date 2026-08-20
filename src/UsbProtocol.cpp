#include "UsbProtocol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "EspBarcodeCore.h"
#include "app_config.h"

using namespace espbarcode;

namespace {
std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
}

void UsbProtocol::begin() {
    line_.reserve(512);
    JsonDocument event;
    event["event"] = "ready";
    event["device"] = app_config::kDeviceName;
    event["protocol"] = app_config::kProtocolVersion;
    event["firmware"] = ESPBARCODE_VERSION;
    send(event);
}

void UsbProtocol::loop() {
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
                send(response);
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

void UsbProtocol::processLine(const std::string& line) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, line);
    if (error) {
        JsonDocument response;
        response["ok"] = false;
        response["error"]["code"] = "invalid_json";
        response["error"]["message"] = error.c_str();
        send(response);
        return;
    }
    if (!document.is<JsonObject>()) {
        JsonDocument response;
        response["ok"] = false;
        response["error"]["code"] = "invalid_request";
        response["error"]["message"] = "request must be a JSON object";
        send(response);
        return;
    }
    dispatch(document.as<JsonObjectConst>());
}

void UsbProtocol::dispatch(JsonObjectConst request) {
    const char* commandText = request["cmd"] | "";
    const std::string command = lower(commandText);
    if (command.empty()) {
        sendError(request, "", "missing_command", "cmd is required");
    } else if (command == "hello" || command == "ping") {
        commandHello(request);
    } else if (command == "capabilities") {
        commandCapabilities(request);
    } else if (command == "status") {
        commandStatus(request);
    } else if (command == "generate") {
        commandGenerate(request);
    } else if (command == "display") {
        commandDisplay(request);
    } else if (command == "close") {
        application_.closeBarcode();
        sendOk(request, "close", "barcode closed");
    } else if (command == "home") {
        application_.showHome("USB requested home screen");
        sendOk(request, "home", "home screen displayed");
    } else if (command == "save") {
        commandSave(request);
    } else if (command == "load") {
        commandLoad(request);
    } else if (command == "delete") {
        commandDelete(request);
    } else if (command == "list") {
        commandList(request);
    } else if (command == "upload_begin") {
        commandUploadBegin(request);
    } else if (command == "upload_chunk") {
        commandUploadChunk(request);
    } else if (command == "upload_end") {
        commandUploadEnd(request);
    } else if (command == "upload_abort") {
        upload_.reset();
        sendOk(request, "upload_abort", "upload discarded");
    } else if (command == "download") {
        commandDownload(request);
    } else if (command == "backlight") {
        const bool on = request["on"] | true;
        application_.setBacklight(on);
        sendOk(request, "backlight", on ? "backlight on" : "backlight off");
    } else if (command == "reboot") {
        sendOk(request, "reboot", "rebooting");
        Serial.flush();
        delay(100);
        ESP.restart();
    } else {
        sendError(request, command.c_str(), "unknown_command", "unsupported command");
    }
}

JsonVariantConst UsbProtocol::value(JsonObjectConst request, const char* key) const {
    JsonVariantConst direct = request[key];
    if (!direct.isNull()) return direct;
    JsonObjectConst options = request["options"].as<JsonObjectConst>();
    return options.isNull() ? JsonVariantConst() : options[key];
}

bool UsbProtocol::parseSpec(JsonObjectConst request,
                            BarcodeSpec& spec,
                            std::string& error) const {
    spec = application_.activeSpec();

    JsonVariantConst typeValue = value(request, "type");
    if (!typeValue.isNull()) {
        if (!typeValue.is<const char*>()) {
            error = "type must be a string";
            return false;
        }
        Symbology type;
        if (!tryParseSymbology(typeValue.as<const char*>(), type)) {
            error = "unknown symbology";
            return false;
        }
        spec.type = type;
    }

    JsonVariantConst dataBase64 = value(request, "data_base64");
    JsonVariantConst data = value(request, "data");
    if (!dataBase64.isNull()) {
        if (!dataBase64.is<const char*>()) {
            error = "data_base64 must be a string";
            return false;
        }
        std::vector<uint8_t> decoded;
        if (!bytesFromBase64(dataBase64.as<const char*>(), decoded)) {
            error = "data_base64 is invalid";
            return false;
        }
        spec.data.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    } else if (!data.isNull()) {
        if (!data.is<const char*>()) {
            error = "data must be a string; use data_base64 for arbitrary bytes";
            return false;
        }
        spec.data = std::string(data.as<const char*>());
    }

    JsonVariantConst eccValue = value(request, "ecc");
    if (!eccValue.isNull()) {
        if (!eccValue.is<const char*>()) {
            error = "ecc must be a string";
            return false;
        }
        ErrorCorrection ecc;
        if (!tryParseErrorCorrection(eccValue.as<const char*>(), ecc)) {
            error = "ecc must be L, M, Q, or H";
            return false;
        }
        spec.ecc = ecc;
    }

    JsonVariantConst rotationValue = value(request, "rotation");
    if (!rotationValue.isNull()) {
        Rotation rotation;
        std::string text;
        if (rotationValue.is<int>()) text = std::to_string(rotationValue.as<int>());
        else if (rotationValue.is<const char*>()) text = rotationValue.as<const char*>();
        else {
            error = "rotation must be a number or string";
            return false;
        }
        if (!tryParseRotation(text, rotation)) {
            error = "rotation must be auto, 0, 90, 180, or 270";
            return false;
        }
        spec.rotation = rotation;
    }

    JsonVariantConst quiet = value(request, "quiet");
    if (!quiet.isNull()) spec.quietZone = std::clamp<int>(quiet.as<int>(), -1, 32);
    JsonVariantConst minimum = value(request, "min_module");
    if (!minimum.isNull()) spec.minModulePixels = static_cast<uint8_t>(std::clamp<int>(minimum.as<int>(), 1, 8));
    JsonVariantConst rect = value(request, "rect");
    if (!rect.isNull()) spec.dataMatrixRectangular = rect.as<bool>();
    JsonVariantConst invert = value(request, "invert");
    if (!invert.isNull()) spec.invert = invert.as<bool>();
    JsonVariantConst checksum = value(request, "checksum");
    if (!checksum.isNull()) spec.checksum = checksum.as<bool>();
    JsonVariantConst qrMin = value(request, "qr_min_version");
    if (!qrMin.isNull()) spec.qrMinVersion = static_cast<uint8_t>(std::clamp<int>(qrMin.as<int>(), 1, 40));
    JsonVariantConst qrMax = value(request, "qr_max_version");
    if (!qrMax.isNull()) spec.qrMaxVersion = static_cast<uint8_t>(std::clamp<int>(qrMax.as<int>(), 1, 40));
    if (spec.qrMaxVersion < spec.qrMinVersion) {
        error = "qr_max_version must be greater than or equal to qr_min_version";
        return false;
    }
    JsonVariantConst security = value(request, "aztec_security");
    if (!security.isNull()) spec.aztecSecurityPercent = static_cast<uint8_t>(std::clamp<int>(security.as<int>(), 1, 90));
    JsonVariantConst layers = value(request, "aztec_layers");
    if (!layers.isNull()) spec.aztecMinLayers = static_cast<uint8_t>(std::clamp<int>(layers.as<int>(), 0, 32));

    if (spec.data.size() > app_config::kMaxPayloadBytes) {
        error = "payload exceeds device limit";
        return false;
    }
    return true;
}

void UsbProtocol::commandHello(JsonObjectConst request) {
    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = request["cmd"] | "hello";
    response["device"] = app_config::kDeviceName;
    response["protocol"] = app_config::kProtocolVersion;
    response["firmware"] = ESPBARCODE_VERSION;
    response["transport"] = "usb-uart-ndjson";
    response["screen"]["width"] = app_config::kScreenWidth;
    response["screen"]["height"] = app_config::kScreenHeight;
    send(response);
}

void UsbProtocol::commandCapabilities(JsonObjectConst request) {
    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "capabilities";
    JsonArray types = response["symbologies"].to<JsonArray>();
    for (const char* type : {"qr", "datamatrix", "aztec", "code128", "gs1-128", "code39",
                             "upca", "ean13", "ean8", "itf", "itf14", "codabar", "msi"}) {
        types.add(type);
    }
    JsonArray commands = response["commands"].to<JsonArray>();
    for (const char* command : {"hello", "capabilities", "status", "generate", "display", "close",
                                "home", "save", "load", "delete", "list", "upload_begin",
                                "upload_chunk", "upload_end", "upload_abort", "download",
                                "backlight", "reboot"}) {
        commands.add(command);
    }
    response["limits"]["payload_bytes"] = app_config::kMaxPayloadBytes;
    response["limits"]["serial_line_bytes"] = app_config::kSerialLineLimit;
    response["limits"]["matrix_width"] = 512;
    response["limits"]["matrix_height"] = 512;
    response["upload"]["encoding"] = "base64-packed-msb-first";
    response["upload"]["chunk_bytes_recommended"] = 384;
    response["raw_matrix"] = true;
    response["standalone_touch_ui"] = true;
    response["persistent_presets"] = true;
    send(response);
}

void UsbProtocol::commandStatus(JsonObjectConst request) {
    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "status";
    response["barcode_visible"] = application_.barcodeVisible();
    response["has_current"] = application_.hasCurrent();
    response["current_raw"] = application_.currentIsRaw();
    response["status"] = application_.statusText();
    response["free_heap"] = ESP.getFreeHeap();
    if (application_.hasCurrent()) {
        const auto& result = application_.currentResult();
        response["current"]["label"] = application_.currentLabel();
        response["current"]["width"] = result.matrix.width();
        response["current"]["height"] = result.matrix.height();
        response["current"]["linear"] = result.linear;
        response["current"]["quiet"] = application_.currentQuietZone();
        response["current"]["rotation"] = toString(application_.currentRotation());
        response["current"]["invert"] = application_.currentInvert();
        response["current"]["bytes"] = result.matrix.packed().size();
    }
    send(response);
}

void UsbProtocol::commandGenerate(JsonObjectConst request) {
    BarcodeSpec spec;
    std::string error;
    if (!parseSpec(request, spec, error)) {
        sendError(request, "generate", "invalid_spec", error);
        return;
    }
    const bool display = request["display"] | true;
    if (!application_.generate(spec, display, error)) {
        sendError(request, "generate", "generation_failed", error);
        return;
    }

    JsonVariantConst saveAs = request["save_as"];
    if (!saveAs.isNull()) {
        if (!saveAs.is<const char*>()) {
            sendError(request, "generate", "invalid_spec", "save_as must be a string");
            return;
        }
        if (!application_.presets().save(saveAs.as<const char*>(), spec, error)) {
            sendError(request, "generate", "save_failed", error);
            return;
        }
    }

    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "generate";
    response["type"] = toString(spec.type);
    response["width"] = application_.currentResult().matrix.width();
    response["height"] = application_.currentResult().matrix.height();
    response["linear"] = application_.currentResult().linear;
    response["quiet"] = application_.currentQuietZone();
    response["displayed"] = display;
    response["normalized_data"] = application_.currentResult().normalizedData;
    send(response);
}

void UsbProtocol::commandDisplay(JsonObjectConst request) {
    std::string error;
    JsonVariantConst name = request["name"];
    if (!name.isNull()) {
        if (!name.is<const char*>()) {
            sendError(request, "display", "invalid_name", "name must be a string");
            return;
        }
        BarcodeSpec spec;
        if (!application_.presets().load(name.as<const char*>(), spec, error) ||
            !application_.generate(spec, true, error)) {
            sendError(request, "display", "display_failed", error);
            return;
        }
    } else if (!application_.displayCurrent(error)) {
        sendError(request, "display", "display_failed", error);
        return;
    }
    sendOk(request, "display", "symbol displayed");
}

void UsbProtocol::commandSave(JsonObjectConst request) {
    const char* name = request["name"] | "";
    if (*name == '\0') {
        sendError(request, "save", "missing_name", "name is required");
        return;
    }
    if (application_.currentIsRaw()) {
        sendError(request, "save", "raw_not_persisted", "raw matrices are transferable but are not preset records");
        return;
    }
    std::string error;
    if (!application_.presets().save(name, application_.activeSpec(), error)) {
        sendError(request, "save", "save_failed", error);
        return;
    }
    sendOk(request, "save", "preset saved");
}

void UsbProtocol::commandLoad(JsonObjectConst request) {
    const char* name = request["name"] | "";
    if (*name == '\0') {
        sendError(request, "load", "missing_name", "name is required");
        return;
    }
    BarcodeSpec spec;
    std::string error;
    if (!application_.presets().load(name, spec, error)) {
        sendError(request, "load", "load_failed", error);
        return;
    }
    const bool display = request["display"] | false;
    if (!application_.generate(spec, display, error)) {
        sendError(request, "load", "generation_failed", error);
        return;
    }
    sendOk(request, "load", display ? "preset loaded and displayed" : "preset loaded");
}

void UsbProtocol::commandDelete(JsonObjectConst request) {
    const char* name = request["name"] | "";
    if (*name == '\0') {
        sendError(request, "delete", "missing_name", "name is required");
        return;
    }
    std::string error;
    if (!application_.presets().remove(name, error)) {
        sendError(request, "delete", "delete_failed", error);
        return;
    }
    sendOk(request, "delete", "preset deleted");
}

void UsbProtocol::commandList(JsonObjectConst request) {
    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "list";
    JsonArray presets = response["presets"].to<JsonArray>();
    for (const auto& name : application_.presets().list()) presets.add(name);
    send(response);
}

void UsbProtocol::commandUploadBegin(JsonObjectConst request) {
    if (upload_.active) {
        sendError(request, "upload_begin", "upload_active", "abort or finish the current upload first");
        return;
    }
    const int width = request["width"] | 0;
    const int height = request["height"] | 0;
    const bool linear = request["linear"] | false;
    if (width < 1 || width > 512 || height < 1 || height > 512 || (linear && height != 1)) {
        sendError(request, "upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1");
        return;
    }
    const std::size_t byteCount = (static_cast<std::size_t>(width) * height + 7U) / 8U;
    if (byteCount > 32768U) {
        sendError(request, "upload_begin", "matrix_too_large", "packed matrix exceeds 32768 bytes");
        return;
    }

    Rotation rotation = Rotation::Auto;
    JsonVariantConst rotationValue = request["rotation"];
    if (!rotationValue.isNull()) {
        std::string text;
        if (rotationValue.is<int>()) text = std::to_string(rotationValue.as<int>());
        else if (rotationValue.is<const char*>()) text = rotationValue.as<const char*>();
        else {
            sendError(request, "upload_begin", "invalid_rotation", "rotation must be a number or string");
            return;
        }
        if (!tryParseRotation(text, rotation)) {
            sendError(request, "upload_begin", "invalid_rotation", "rotation must be auto, 0, 90, 180, or 270");
            return;
        }
    }

    upload_.active = true;
    upload_.width = static_cast<uint16_t>(width);
    upload_.height = static_cast<uint16_t>(height);
    upload_.linear = linear;
    upload_.quiet = static_cast<uint8_t>(std::clamp<int>(request["quiet"] | 4, 0, 32));
    upload_.rotation = rotation;
    upload_.invert = request["invert"] | false;
    upload_.display = request["display"] | true;
    upload_.label = std::string(request["label"] | "Uploaded matrix");
    upload_.bytes.assign(byteCount, 0U);
    upload_.nextOffset = 0;

    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "upload_begin";
    response["bytes_expected"] = byteCount;
    response["next_offset"] = 0;
    send(response);
}

void UsbProtocol::commandUploadChunk(JsonObjectConst request) {
    if (!upload_.active) {
        sendError(request, "upload_chunk", "no_upload", "upload_begin is required");
        return;
    }
    JsonVariantConst offsetValue = request["offset"];
    if (offsetValue.isNull() || !offsetValue.is<unsigned long>()) {
        sendError(request, "upload_chunk", "missing_offset", "offset is required and must be an unsigned integer");
        return;
    }
    const std::size_t offset = static_cast<std::size_t>(offsetValue.as<unsigned long>());
    if (offset != upload_.nextOffset) {
        sendError(request, "upload_chunk", "unexpected_offset", "chunks must be sequential");
        return;
    }
    const char* encoded = request["data"] | "";
    std::vector<uint8_t> chunk;
    if (*encoded == '\0' || !bytesFromBase64(encoded, chunk)) {
        sendError(request, "upload_chunk", "invalid_base64", "data must be a base64 string");
        return;
    }
    if (offset + chunk.size() > upload_.bytes.size()) {
        sendError(request, "upload_chunk", "overflow", "chunk exceeds declared matrix size");
        return;
    }
    std::copy(chunk.begin(), chunk.end(), upload_.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    upload_.nextOffset += chunk.size();

    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "upload_chunk";
    response["accepted"] = chunk.size();
    response["next_offset"] = upload_.nextOffset;
    send(response);
}

void UsbProtocol::commandUploadEnd(JsonObjectConst request) {
    if (!upload_.active) {
        sendError(request, "upload_end", "no_upload", "upload_begin is required");
        return;
    }
    if (upload_.nextOffset != upload_.bytes.size()) {
        sendError(request, "upload_end", "incomplete", "not all declared bytes were received");
        return;
    }
    JsonVariantConst expectedCrc = request["crc32"];
    const uint32_t actualCrc = crc32(upload_.bytes);
    if (!expectedCrc.isNull() && expectedCrc.as<uint32_t>() != actualCrc) {
        sendError(request, "upload_end", "crc_mismatch", "uploaded bytes failed CRC32 validation");
        return;
    }

    BitMatrix matrix(upload_.width, upload_.height);
    matrix.packed() = upload_.bytes;
    std::string error;
    const bool success = application_.setUploadedMatrix(std::move(matrix),
                                                         upload_.linear,
                                                         upload_.quiet,
                                                         upload_.rotation,
                                                         upload_.invert,
                                                         upload_.label,
                                                         upload_.display,
                                                         error);
    const bool displayed = upload_.display;
    upload_.reset();
    if (!success) {
        sendError(request, "upload_end", "display_failed", error);
        return;
    }

    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = "upload_end";
    response["crc32"] = actualCrc;
    response["displayed"] = displayed;
    send(response);
}

void UsbProtocol::commandDownload(JsonObjectConst request) {
    if (!application_.hasCurrent()) {
        sendError(request, "download", "no_symbol", "no current symbol");
        return;
    }
    const auto& result = application_.currentResult();
    const auto& bytes = result.matrix.packed();
    const std::size_t chunkSize = static_cast<std::size_t>(std::clamp<int>(request["chunk_bytes"] | 384, 48, 768));
    const uint32_t checksum = crc32(bytes);

    JsonDocument begin;
    addId(begin, request);
    begin["ok"] = true;
    begin["cmd"] = "download";
    begin["event"] = "download_begin";
    begin["width"] = result.matrix.width();
    begin["height"] = result.matrix.height();
    begin["linear"] = result.linear;
    begin["quiet"] = application_.currentQuietZone();
    begin["rotation"] = toString(application_.currentRotation());
    begin["invert"] = application_.currentInvert();
    begin["label"] = application_.currentLabel();
    begin["bytes"] = bytes.size();
    begin["encoding"] = "base64-packed-msb-first";
    begin["crc32"] = checksum;
    send(begin);

    for (std::size_t offset = 0; offset < bytes.size(); offset += chunkSize) {
        const std::size_t count = std::min(chunkSize, bytes.size() - offset);
        std::vector<uint8_t> chunk(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
        JsonDocument response;
        addId(response, request);
        response["ok"] = true;
        response["cmd"] = "download";
        response["event"] = "download_chunk";
        response["offset"] = offset;
        response["data"] = bytesToBase64(chunk);
        send(response);
        delay(0);
    }

    JsonDocument end;
    addId(end, request);
    end["ok"] = true;
    end["cmd"] = "download";
    end["event"] = "download_end";
    end["bytes"] = bytes.size();
    end["crc32"] = checksum;
    send(end);
}

void UsbProtocol::addId(JsonDocument& response, JsonObjectConst request) const {
    JsonVariantConst id = request["id"];
    if (!id.isNull()) response["id"].set(id);
}

void UsbProtocol::send(JsonDocument& response) const {
    serializeJson(response, Serial);
    Serial.write('\n');
}

void UsbProtocol::sendOk(JsonObjectConst request, const char* command, const char* message) const {
    JsonDocument response;
    addId(response, request);
    response["ok"] = true;
    response["cmd"] = command;
    if (message != nullptr) response["message"] = message;
    send(response);
}

void UsbProtocol::sendError(JsonObjectConst request,
                            const char* command,
                            const char* code,
                            const std::string& message) const {
    JsonDocument response;
    addId(response, request);
    response["ok"] = false;
    if (command != nullptr && *command != '\0') response["cmd"] = command;
    response["error"]["code"] = code;
    response["error"]["message"] = message;
    send(response);
}

uint32_t UsbProtocol::crc32(const std::vector<uint8_t>& bytes) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}
