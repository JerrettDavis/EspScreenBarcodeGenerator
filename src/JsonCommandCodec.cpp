#include "JsonCommandCodec.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "app_config.h"

using namespace espbarcode;

namespace esplink {

namespace {

bool decodeRotation(JsonVariantConst rotationValue, Rotation& out, std::string& errorMessage) {
    if (rotationValue.isNull()) return true;  // caller keeps its current default
    std::string text;
    if (rotationValue.is<int>()) text = std::to_string(rotationValue.as<int>());
    else if (rotationValue.is<const char*>()) text = rotationValue.as<const char*>();
    else { errorMessage = "rotation must be a number or string"; return false; }
    if (!tryParseRotation(text, out)) {
        errorMessage = "rotation must be auto, 0, 90, 180, or 270";
        return false;
    }
    return true;
}

}  // namespace

JsonVariantConst JsonCommandCodec::value(JsonObjectConst request, const char* key) {
    JsonVariantConst direct = request[key];
    if (!direct.isNull()) return direct;
    JsonObjectConst options = request["options"].as<JsonObjectConst>();
    return options.isNull() ? JsonVariantConst() : options[key];
}

bool JsonCommandCodec::parseSpec(JsonObjectConst request, const BarcodeSpec& base, BarcodeSpec& spec,
                                 std::string& errorCode, std::string& errorMessage) {
    spec = base;

    JsonVariantConst typeValue = value(request, "type");
    if (!typeValue.isNull()) {
        if (!typeValue.is<const char*>()) {
            errorCode = "invalid_spec";
            errorMessage = "type must be a string";
            return false;
        }
        Symbology type;
        if (!tryParseSymbology(typeValue.as<const char*>(), type)) {
            errorCode = "invalid_spec";
            errorMessage = "unknown symbology";
            return false;
        }
        spec.type = type;
    }

    JsonVariantConst dataBase64 = value(request, "data_base64");
    JsonVariantConst data = value(request, "data");
    if (!dataBase64.isNull()) {
        if (!dataBase64.is<const char*>()) {
            errorCode = "invalid_spec";
            errorMessage = "data_base64 must be a string";
            return false;
        }
        std::vector<uint8_t> decoded;
        if (!bytesFromBase64(dataBase64.as<const char*>(), decoded)) {
            errorCode = "invalid_spec";
            errorMessage = "data_base64 is invalid";
            return false;
        }
        spec.data.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    } else if (!data.isNull()) {
        if (!data.is<const char*>()) {
            errorCode = "invalid_spec";
            errorMessage = "data must be a string; use data_base64 for arbitrary bytes";
            return false;
        }
        spec.data = std::string(data.as<const char*>());
    }

    JsonVariantConst eccValue = value(request, "ecc");
    if (!eccValue.isNull()) {
        if (!eccValue.is<const char*>()) {
            errorCode = "invalid_spec";
            errorMessage = "ecc must be a string";
            return false;
        }
        ErrorCorrection ecc;
        if (!tryParseErrorCorrection(eccValue.as<const char*>(), ecc)) {
            errorCode = "invalid_spec";
            errorMessage = "ecc must be L, M, Q, or H";
            return false;
        }
        spec.ecc = ecc;
    }

    Rotation rotation = spec.rotation;
    if (!decodeRotation(value(request, "rotation"), rotation, errorMessage)) {
        errorCode = "invalid_spec";
        return false;
    }
    spec.rotation = rotation;

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
        errorCode = "invalid_spec";
        errorMessage = "qr_max_version must be greater than or equal to qr_min_version";
        return false;
    }
    JsonVariantConst security = value(request, "aztec_security");
    if (!security.isNull()) spec.aztecSecurityPercent = static_cast<uint8_t>(std::clamp<int>(security.as<int>(), 1, 90));
    JsonVariantConst layers = value(request, "aztec_layers");
    if (!layers.isNull()) spec.aztecMinLayers = static_cast<uint8_t>(std::clamp<int>(layers.as<int>(), 0, 32));

    if (spec.data.size() > app_config::kMaxPayloadBytes) {
        errorCode = "invalid_spec";
        errorMessage = "payload exceeds device limit";
        return false;
    }
    return true;
}

bool JsonCommandCodec::decode(const std::string& commandName, JsonObjectConst request, const IBarcodeDevice& device,
                              Command& out, std::string& errorCode, std::string& errorMessage) {
    if (commandName == "hello" || commandName == "ping") {
        out = HelloCommand{};
        return true;
    }
    if (commandName == "capabilities") {
        out = CapabilitiesCommand{};
        return true;
    }
    if (commandName == "status") {
        out = StatusCommand{};
        return true;
    }
    if (commandName == "generate") {
        BarcodeSpec spec;
        if (!parseSpec(request, device.activeSpec(), spec, errorCode, errorMessage)) return false;
        GenerateCommand command;
        command.spec = spec;
        command.display = request["display"] | true;
        JsonVariantConst saveAs = request["save_as"];
        if (!saveAs.isNull()) {
            if (!saveAs.is<const char*>()) {
                errorCode = "invalid_spec";
                errorMessage = "save_as must be a string";
                return false;
            }
            command.saveAs = std::string(saveAs.as<const char*>());
        }
        out = command;
        return true;
    }
    if (commandName == "display") {
        JsonVariantConst name = request["name"];
        DisplayCommand command;
        if (!name.isNull()) {
            if (!name.is<const char*>()) {
                errorCode = "invalid_name";
                errorMessage = "name must be a string";
                return false;
            }
            command.presetName = std::string(name.as<const char*>());
        }
        out = command;
        return true;
    }
    if (commandName == "close") {
        out = CloseCommand{};
        return true;
    }
    if (commandName == "home") {
        out = HomeCommand{};
        return true;
    }
    if (commandName == "save") {
        SaveCommand command;
        command.name = std::string(request["name"] | "");
        out = command;
        return true;
    }
    if (commandName == "load") {
        LoadCommand command;
        command.name = std::string(request["name"] | "");
        command.display = request["display"] | false;
        out = command;
        return true;
    }
    if (commandName == "delete") {
        DeleteCommand command;
        command.name = std::string(request["name"] | "");
        out = command;
        return true;
    }
    if (commandName == "list") {
        out = ListCommand{};
        return true;
    }
    if (commandName == "upload_begin") {
        UploadBeginCommand command;
        const int rawWidth = request["width"] | 0;
        const int rawHeight = request["height"] | 0;
        command.width = static_cast<uint16_t>(std::clamp(rawWidth, 0, 65535));
        command.height = static_cast<uint16_t>(std::clamp(rawHeight, 0, 65535));
        command.linear = request["linear"] | false;
        command.quiet = static_cast<uint8_t>(std::clamp<int>(request["quiet"] | 4, 0, 32));
        Rotation rotation = Rotation::Auto;
        if (!decodeRotation(request["rotation"], rotation, errorMessage)) {
            errorCode = "invalid_rotation";
            return false;
        }
        command.rotation = rotation;
        command.invert = request["invert"] | false;
        command.display = request["display"] | true;
        command.label = std::string(request["label"] | "Uploaded matrix");
        out = command;
        return true;
    }
    if (commandName == "upload_chunk") {
        JsonVariantConst offsetValue = request["offset"];
        if (offsetValue.isNull() || !offsetValue.is<unsigned long>()) {
            errorCode = "missing_offset";
            errorMessage = "offset is required and must be an unsigned integer";
            return false;
        }
        const char* encoded = request["data"] | "";
        std::vector<uint8_t> chunk;
        if (*encoded == '\0' || !bytesFromBase64(encoded, chunk)) {
            errorCode = "invalid_base64";
            errorMessage = "data must be a base64 string";
            return false;
        }
        UploadChunkCommand command;
        command.offset = static_cast<std::size_t>(offsetValue.as<unsigned long>());
        command.data = std::move(chunk);
        out = command;
        return true;
    }
    if (commandName == "upload_end") {
        UploadEndCommand command;
        JsonVariantConst crc = request["crc32"];
        if (!crc.isNull()) command.expectedCrc32 = crc.as<uint32_t>();
        out = command;
        return true;
    }
    if (commandName == "upload_abort") {
        out = UploadAbortCommand{};
        return true;
    }
    if (commandName == "download") {
        DownloadCommand command;
        command.chunkBytes = static_cast<std::size_t>(request["chunk_bytes"] | 384);
        out = command;
        return true;
    }
    if (commandName == "backlight") {
        BacklightCommand command;
        command.on = request["on"] | true;
        out = command;
        return true;
    }
    if (commandName == "reboot") {
        out = RebootCommand{};
        return true;
    }

    // Unreachable in practice — SerialLegacyEndpoint::processLine never calls decode()
    // for a name outside the eighteen matched above. Kept only as a defensive default.
    errorCode = "unknown_command";
    errorMessage = "unsupported command";
    return false;
}

void JsonCommandCodec::addId(JsonDocument& out, JsonObjectConst request) {
    JsonVariantConst id = request["id"];
    if (!id.isNull()) out["id"].set(id);
}

void JsonCommandCodec::encodeBody(const Response& response, JsonObject body) {
    std::visit([&](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, HelloResponse>) {
            body["device"] = value.device;
            body["protocol"] = value.protocol;
            body["firmware"] = value.firmware;
            body["transport"] = value.transport;
            body["screen"]["width"] = value.screenWidth;
            body["screen"]["height"] = value.screenHeight;
        } else if constexpr (std::is_same_v<T, SimpleOkResponse>) {
            if (value.message.has_value()) body["message"] = *value.message;
        } else if constexpr (std::is_same_v<T, CapabilitiesResponse>) {
            JsonArray symbologies = body["symbologies"].to<JsonArray>();
            for (const auto& symbology : value.symbologies) symbologies.add(symbology);
            JsonArray commands = body["commands"].to<JsonArray>();
            for (const auto& command : value.commands) commands.add(command);
            body["limits"]["payload_bytes"] = value.payloadBytes;
            body["limits"]["serial_line_bytes"] = value.serialLineBytes;
            body["limits"]["matrix_width"] = value.matrixWidth;
            body["limits"]["matrix_height"] = value.matrixHeight;
            body["upload"]["encoding"] = value.uploadEncoding;
            body["upload"]["chunk_bytes_recommended"] = value.uploadChunkBytesRecommended;
            body["raw_matrix"] = value.rawMatrix;
            body["standalone_touch_ui"] = value.standaloneTouchUi;
            body["persistent_presets"] = value.persistentPresets;
        } else if constexpr (std::is_same_v<T, StatusResponse>) {
            body["barcode_visible"] = value.barcodeVisible;
            body["has_current"] = value.hasCurrent;
            body["current_raw"] = value.currentRaw;
            body["status"] = value.status;
            body["free_heap"] = value.freeHeap;
            if (value.current.has_value()) {
                const auto& current = *value.current;
                body["current"]["label"] = current.label;
                body["current"]["width"] = current.width;
                body["current"]["height"] = current.height;
                body["current"]["linear"] = current.linear;
                body["current"]["quiet"] = current.quiet;
                body["current"]["rotation"] = current.rotation;
                body["current"]["invert"] = current.invert;
                body["current"]["bytes"] = current.bytes;
            }
        } else if constexpr (std::is_same_v<T, GenerateResponse>) {
            body["type"] = value.type;
            body["width"] = value.width;
            body["height"] = value.height;
            body["linear"] = value.linear;
            body["quiet"] = value.quiet;
            body["displayed"] = value.displayed;
            body["normalized_data"] = value.normalizedData;
        } else if constexpr (std::is_same_v<T, ListResponse>) {
            JsonArray presets = body["presets"].to<JsonArray>();
            for (const auto& name : value.presets) presets.add(name);
        } else if constexpr (std::is_same_v<T, UploadBeginResponse>) {
            body["bytes_expected"] = value.bytesExpected;
            body["next_offset"] = value.nextOffset;
        } else if constexpr (std::is_same_v<T, UploadChunkResponse>) {
            body["accepted"] = value.accepted;
            body["next_offset"] = value.nextOffset;
        } else if constexpr (std::is_same_v<T, UploadEndResponse>) {
            body["crc32"] = value.crc32;
            body["displayed"] = value.displayed;
        } else if constexpr (std::is_same_v<T, DownloadBeginEvent>) {
            body["event"] = "download_begin";
            body["width"] = value.width;
            body["height"] = value.height;
            body["linear"] = value.linear;
            body["quiet"] = value.quiet;
            body["rotation"] = value.rotation;
            body["invert"] = value.invert;
            body["label"] = value.label;
            body["bytes"] = value.bytes;
            body["encoding"] = value.encoding;
            body["crc32"] = value.crc32;
        } else if constexpr (std::is_same_v<T, DownloadChunkEvent>) {
            body["event"] = "download_chunk";
            body["offset"] = value.offset;
            body["data"] = bytesToBase64(value.data);
        } else if constexpr (std::is_same_v<T, DownloadEndEvent>) {
            body["event"] = "download_end";
            body["bytes"] = value.bytes;
            body["crc32"] = value.crc32;
        }
    }, response);
}

void JsonCommandCodec::encode(const Response& response, JsonObjectConst request, JsonDocument& out) {
    addId(out, request);
    out["ok"] = true;
    std::visit([&](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, HelloResponse>) {
            out["cmd"] = request["cmd"] | "hello";
        } else if constexpr (std::is_same_v<T, SimpleOkResponse>) {
            out["cmd"] = value.command;
        } else if constexpr (std::is_same_v<T, CapabilitiesResponse>) {
            out["cmd"] = "capabilities";
        } else if constexpr (std::is_same_v<T, StatusResponse>) {
            out["cmd"] = "status";
        } else if constexpr (std::is_same_v<T, GenerateResponse>) {
            out["cmd"] = "generate";
        } else if constexpr (std::is_same_v<T, ListResponse>) {
            out["cmd"] = "list";
        } else if constexpr (std::is_same_v<T, UploadBeginResponse>) {
            out["cmd"] = "upload_begin";
        } else if constexpr (std::is_same_v<T, UploadChunkResponse>) {
            out["cmd"] = "upload_chunk";
        } else if constexpr (std::is_same_v<T, UploadEndResponse>) {
            out["cmd"] = "upload_end";
        } else if constexpr (std::is_same_v<T, DownloadBeginEvent> || std::is_same_v<T, DownloadChunkEvent> ||
                             std::is_same_v<T, DownloadEndEvent>) {
            out["cmd"] = "download";
        }
    }, response);
    encodeBody(response, out.as<JsonObject>());
}

void JsonCommandCodec::encodeError(const ProtocolError& error, JsonObjectConst request, JsonDocument& out) {
    addId(out, request);
    out["ok"] = false;
    if (!error.command.empty()) out["cmd"] = error.command;
    out["error"]["code"] = error.code;
    out["error"]["message"] = error.message;
}

}  // namespace esplink
