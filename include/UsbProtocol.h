#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "BarcodeApplication.h"

class UsbProtocol {
public:
    explicit UsbProtocol(BarcodeApplication& application) : application_(application) {}

    void begin();
    void loop();

private:
    struct UploadState {
        bool active = false;
        uint16_t width = 0;
        uint16_t height = 0;
        bool linear = false;
        uint8_t quiet = 4;
        espbarcode::Rotation rotation = espbarcode::Rotation::Auto;
        bool invert = false;
        bool display = true;
        std::string label;
        std::vector<uint8_t> bytes;
        std::size_t nextOffset = 0;

        void reset() { *this = UploadState{}; }
    };

    void processLine(const std::string& line);
    void dispatch(JsonObjectConst request);
    void commandHello(JsonObjectConst request);
    void commandCapabilities(JsonObjectConst request);
    void commandStatus(JsonObjectConst request);
    void commandGenerate(JsonObjectConst request);
    void commandDisplay(JsonObjectConst request);
    void commandSave(JsonObjectConst request);
    void commandLoad(JsonObjectConst request);
    void commandDelete(JsonObjectConst request);
    void commandList(JsonObjectConst request);
    void commandUploadBegin(JsonObjectConst request);
    void commandUploadChunk(JsonObjectConst request);
    void commandUploadEnd(JsonObjectConst request);
    void commandDownload(JsonObjectConst request);

    bool parseSpec(JsonObjectConst request,
                   espbarcode::BarcodeSpec& spec,
                   std::string& error) const;
    JsonVariantConst value(JsonObjectConst request, const char* key) const;
    void addId(JsonDocument& response, JsonObjectConst request) const;
    void send(JsonDocument& response) const;
    void sendOk(JsonObjectConst request, const char* command, const char* message = nullptr) const;
    void sendError(JsonObjectConst request,
                   const char* command,
                   const char* code,
                   const std::string& message) const;
    static uint32_t crc32(const std::vector<uint8_t>& bytes);

    BarcodeApplication& application_;
    std::string line_;
    bool discardingLine_ = false;
    UploadState upload_;
};
