#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "EspBarcodeCore.h"

namespace esplink {

struct HelloCommand {};
struct CapabilitiesCommand {};
struct StatusCommand {};

struct GenerateCommand {
    espbarcode::BarcodeSpec spec;
    bool display = true;
    std::optional<std::string> saveAs;
};

struct DisplayCommand {
    std::optional<std::string> presetName;
};

struct CloseCommand {};
struct HomeCommand {};

struct SaveCommand { std::string name; };

struct LoadCommand {
    std::string name;
    bool display = false;
};

struct DeleteCommand { std::string name; };
struct ListCommand {};

struct UploadBeginCommand {
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 4;
    espbarcode::Rotation rotation = espbarcode::Rotation::Auto;
    bool invert = false;
    bool display = true;
    std::string label;
};

struct UploadChunkCommand {
    std::size_t offset = 0;
    std::vector<uint8_t> data;
};

struct UploadEndCommand {
    std::optional<uint32_t> expectedCrc32;
};

struct UploadAbortCommand {};

struct DownloadCommand {
    std::size_t chunkBytes = 384;
};

struct BacklightCommand { bool on = true; };
struct RebootCommand {};

using Command = std::variant<
    HelloCommand, CapabilitiesCommand, StatusCommand, GenerateCommand, DisplayCommand,
    CloseCommand, HomeCommand, SaveCommand, LoadCommand, DeleteCommand, ListCommand,
    UploadBeginCommand, UploadChunkCommand, UploadEndCommand, UploadAbortCommand,
    DownloadCommand, BacklightCommand, RebootCommand>;

struct HelloResponse {
    std::string device;
    std::string protocol;
    std::string firmware;
    std::string transport;
    uint16_t screenWidth = 0;
    uint16_t screenHeight = 0;
};

struct CapabilitiesResponse {
    std::vector<std::string> symbologies;
    std::vector<std::string> commands;
    std::size_t payloadBytes = 0;
    std::size_t serialLineBytes = 0;
    uint16_t matrixWidth = 0;
    uint16_t matrixHeight = 0;
    std::string uploadEncoding;
    std::size_t uploadChunkBytesRecommended = 0;
    bool rawMatrix = true;
    bool standaloneTouchUi = true;
    bool persistentPresets = true;
};

struct CurrentSymbolInfo {
    std::string label;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 0;
    std::string rotation;
    bool invert = false;
    std::size_t bytes = 0;
};

struct StatusResponse {
    bool barcodeVisible = false;
    bool hasCurrent = false;
    bool currentRaw = false;
    std::string status;
    uint32_t freeHeap = 0;
    std::optional<CurrentSymbolInfo> current;
};

struct GenerateResponse {
    std::string type;
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 0;
    bool displayed = false;
    std::string normalizedData;
};

// Used for display/close/home/save/load/delete/backlight/reboot/upload_abort —
// every v1 command whose success response is only {"ok":true,"cmd":...,"message"?:...}.
struct SimpleOkResponse {
    std::string command;
    std::optional<std::string> message;
};

struct ListResponse { std::vector<std::string> presets; };

struct UploadBeginResponse {
    std::size_t bytesExpected = 0;
    std::size_t nextOffset = 0;
};

struct UploadChunkResponse {
    std::size_t accepted = 0;
    std::size_t nextOffset = 0;
};

struct UploadEndResponse {
    uint32_t crc32 = 0;
    bool displayed = false;
};

struct DownloadBeginEvent {
    uint16_t width = 0;
    uint16_t height = 0;
    bool linear = false;
    uint8_t quiet = 0;
    std::string rotation;
    bool invert = false;
    std::string label;
    std::size_t bytes = 0;
    std::string encoding;
    uint32_t crc32 = 0;
};

struct DownloadChunkEvent {
    std::size_t offset = 0;
    std::vector<uint8_t> data;
};

struct DownloadEndEvent {
    std::size_t bytes = 0;
    uint32_t crc32 = 0;
};

using Response = std::variant<
    HelloResponse, CapabilitiesResponse, StatusResponse, GenerateResponse, SimpleOkResponse,
    ListResponse, UploadBeginResponse, UploadChunkResponse, UploadEndResponse,
    DownloadBeginEvent, DownloadChunkEvent, DownloadEndEvent>;

struct ProtocolError {
    std::string command;  // may be empty, matching UsbProtocol::sendError's optional cmd
    std::string code;
    std::string message;
};

}  // namespace esplink
