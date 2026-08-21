#pragma once

#include <ArduinoJson.h>

#include <string>

#include "ApplicationPorts.h"
#include "ProtocolCommands.h"

namespace esplink {

class JsonCommandCodec {
public:
    // `commandName` is already lower-cased and known non-empty — the caller
    // (SerialLegacyEndpoint) has already handled "missing_command". `device` supplies
    // the "merge with the currently active spec" defaults `generate` needs (see Step 2).
    // Returns false with `errorCode`/`errorMessage` set for a command-specific decode
    // failure (invalid_spec, invalid_rotation, invalid_base64, missing_offset,
    // invalid_name); returns true and populates `out` otherwise.
    static bool decode(const std::string& commandName, JsonObjectConst request, const IBarcodeDevice& device,
                       Command& out, std::string& errorCode, std::string& errorMessage);

    static void encode(const Response& response, JsonObjectConst request, JsonDocument& out);
    static void encodeError(const ProtocolError& error, JsonObjectConst request, JsonDocument& out);

private:
    static bool parseSpec(JsonObjectConst request, const espbarcode::BarcodeSpec& base,
                          espbarcode::BarcodeSpec& spec, std::string& errorCode, std::string& errorMessage);
    static JsonVariantConst value(JsonObjectConst request, const char* key);
    static void addId(JsonDocument& out, JsonObjectConst request);
};

}  // namespace esplink
