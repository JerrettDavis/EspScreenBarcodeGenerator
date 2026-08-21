#include "ControlProtocolEngine.h"

#include <algorithm>
#include <variant>

#include "CommandCatalog.h"
#include "Crc32.h"

namespace esplink {

const char* commandCatalogName(const Command& command) {
    struct Visitor {
        const char* operator()(const HelloCommand&) const { return "hello"; }
        const char* operator()(const CapabilitiesCommand&) const { return "capabilities"; }
        const char* operator()(const StatusCommand&) const { return "status"; }
        const char* operator()(const GenerateCommand&) const { return "generate"; }
        const char* operator()(const DisplayCommand&) const { return "display"; }
        const char* operator()(const CloseCommand&) const { return "close"; }
        const char* operator()(const HomeCommand&) const { return "home"; }
        const char* operator()(const SaveCommand&) const { return "save"; }
        const char* operator()(const LoadCommand&) const { return "load"; }
        const char* operator()(const DeleteCommand&) const { return "delete"; }
        const char* operator()(const ListCommand&) const { return "list"; }
        const char* operator()(const UploadBeginCommand&) const { return "upload_begin"; }
        const char* operator()(const UploadChunkCommand&) const { return "upload_chunk"; }
        const char* operator()(const UploadEndCommand&) const { return "upload_end"; }
        const char* operator()(const UploadAbortCommand&) const { return "upload_abort"; }
        const char* operator()(const DownloadCommand&) const { return "download"; }
        const char* operator()(const BacklightCommand&) const { return "backlight"; }
        const char* operator()(const RebootCommand&) const { return "reboot"; }
    };
    return std::visit(Visitor{}, command);
}

ControlProtocolEngine::ControlProtocolEngine(IBarcodeDevice& device, IPresetRepository& presets,
                                             IDeviceControl& deviceControl, std::string firmwareVersion)
    : device_(device), presets_(presets), deviceControl_(deviceControl), firmwareVersion_(std::move(firmwareVersion)) {}

void ControlProtocolEngine::handle(ControlSession& session, const Command& command, OperationId operationId,
                                   const char* transportName, IControlResponseSink& sink) {
    if (std::holds_alternative<DownloadCommand>(command)) {
        handleDownload(std::get<DownloadCommand>(command), sink);
        return;
    }

    const CommandDescriptor* descriptor = findCommandDescriptor(commandCatalogName(command));
    const bool replayable = descriptor != nullptr && descriptor->idempotency == Idempotency::ReplayResult;

    if (replayable) {
        if (auto cached = session.lookupCachedResult(operationId)) {
            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ProtocolError>) sink.sendError(value);
                else sink.send(value);
            }, *cached);
            return;
        }
    }

    CommandResult result = dispatchSingle(session, command, transportName);

    if (replayable) session.cacheResult(operationId, result);

    std::visit([&](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ProtocolError>) sink.sendError(value);
        else sink.send(value);
    }, result);

    if (std::holds_alternative<RebootCommand>(command) && std::holds_alternative<Response>(result)) {
        deviceControl_.reboot();
    }
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::dispatchSingle(ControlSession& session,
                                                                           const Command& command,
                                                                           const char* transportName) {
    if (std::holds_alternative<HelloCommand>(command)) return handleHello(transportName);
    if (std::holds_alternative<CapabilitiesCommand>(command)) return handleCapabilities();
    if (std::holds_alternative<StatusCommand>(command)) return handleStatus();
    if (std::holds_alternative<GenerateCommand>(command)) return handleGenerate(std::get<GenerateCommand>(command));
    if (std::holds_alternative<DisplayCommand>(command)) return handleDisplay(std::get<DisplayCommand>(command));
    if (std::holds_alternative<CloseCommand>(command)) return handleClose();
    if (std::holds_alternative<HomeCommand>(command)) return handleHome(transportName);
    if (std::holds_alternative<SaveCommand>(command)) return handleSave(std::get<SaveCommand>(command));
    if (std::holds_alternative<LoadCommand>(command)) return handleLoad(std::get<LoadCommand>(command));
    if (std::holds_alternative<DeleteCommand>(command)) return handleDelete(std::get<DeleteCommand>(command));
    if (std::holds_alternative<ListCommand>(command)) return handleList();
    if (std::holds_alternative<UploadBeginCommand>(command)) return handleUploadBegin(session, std::get<UploadBeginCommand>(command));
    if (std::holds_alternative<UploadChunkCommand>(command)) return handleUploadChunk(session, std::get<UploadChunkCommand>(command));
    if (std::holds_alternative<UploadEndCommand>(command)) return handleUploadEnd(session, std::get<UploadEndCommand>(command));
    if (std::holds_alternative<UploadAbortCommand>(command)) return handleUploadAbort(session);
    if (std::holds_alternative<BacklightCommand>(command)) return handleBacklight(std::get<BacklightCommand>(command));
    if (std::holds_alternative<RebootCommand>(command)) return handleReboot();
    return ProtocolError{"", "unknown_command", "unsupported command"};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleHello(const char* transportName) const {
    return Response{HelloResponse{"EspScreenBarcodeGenerator", "1.0", firmwareVersion_, transportName,
                                  320, 480}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleCapabilities() const {
    CapabilitiesResponse response;
    response.symbologies = {"qr", "datamatrix", "aztec", "code128", "gs1-128", "code39",
                            "upca", "ean13", "ean8", "itf", "itf14", "codabar", "msi"};
    for (const auto& d : kCommandCatalog) response.commands.emplace_back(d.name);
    response.payloadBytes = 2048;
    response.serialLineBytes = 4096;
    response.matrixWidth = 512;
    response.matrixHeight = 512;
    response.uploadEncoding = "base64-packed-msb-first";
    response.uploadChunkBytesRecommended = 384;
    response.rawMatrix = true;
    response.standaloneTouchUi = true;
    response.persistentPresets = true;
    return Response{response};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleStatus() const {
    StatusResponse response;
    response.barcodeVisible = device_.barcodeVisible();
    response.hasCurrent = device_.hasCurrent();
    response.currentRaw = device_.currentIsRaw();
    response.status = device_.statusText();
    response.freeHeap = deviceControl_.freeHeapBytes();
    if (device_.hasCurrent()) {
        const auto& result = device_.currentResult();
        CurrentSymbolInfo current;
        current.label = device_.currentLabel();
        current.width = result.matrix.width();
        current.height = result.matrix.height();
        current.linear = result.linear;
        current.quiet = device_.currentQuietZone();
        current.rotation = espbarcode::toString(device_.currentRotation());
        current.invert = device_.currentInvert();
        current.bytes = result.matrix.packed().size();
        response.current = current;
    }
    return Response{response};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleGenerate(const GenerateCommand& command) {
    std::string error;
    if (!device_.generate(command.spec, command.display, error)) {
        return ProtocolError{"generate", "generation_failed", error};
    }
    if (command.saveAs.has_value()) {
        if (!presets_.save(*command.saveAs, command.spec, error)) {
            return ProtocolError{"generate", "save_failed", error};
        }
    }
    GenerateResponse response;
    response.type = espbarcode::toString(command.spec.type);
    response.width = device_.currentResult().matrix.width();
    response.height = device_.currentResult().matrix.height();
    response.linear = device_.currentResult().linear;
    response.quiet = device_.currentQuietZone();
    response.displayed = command.display;
    response.normalizedData = device_.currentResult().normalizedData;
    return Response{response};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleDisplay(const DisplayCommand& command) {
    std::string error;
    if (command.presetName.has_value()) {
        espbarcode::BarcodeSpec spec;
        if (!presets_.load(*command.presetName, spec, error) || !device_.generate(spec, true, error)) {
            return ProtocolError{"display", "display_failed", error};
        }
    } else if (!device_.displayCurrent(error)) {
        return ProtocolError{"display", "display_failed", error};
    }
    return Response{SimpleOkResponse{"display", "symbol displayed"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleClose() {
    device_.closeBarcode();
    return Response{SimpleOkResponse{"close", "barcode closed"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleHome(const char* transportName) {
    device_.showHome(std::string(transportName) + " requested home screen");
    return Response{SimpleOkResponse{"home", "home screen displayed"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleSave(const SaveCommand& command) {
    if (command.name.empty()) return ProtocolError{"save", "missing_name", "name is required"};
    if (device_.currentIsRaw()) {
        return ProtocolError{"save", "raw_not_persisted", "raw matrices are transferable but are not preset records"};
    }
    std::string error;
    if (!presets_.save(command.name, device_.activeSpec(), error)) {
        return ProtocolError{"save", "save_failed", error};
    }
    return Response{SimpleOkResponse{"save", "preset saved"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleLoad(const LoadCommand& command) {
    if (command.name.empty()) return ProtocolError{"load", "missing_name", "name is required"};
    espbarcode::BarcodeSpec spec;
    std::string error;
    if (!presets_.load(command.name, spec, error)) return ProtocolError{"load", "load_failed", error};
    if (!device_.generate(spec, command.display, error)) return ProtocolError{"load", "generation_failed", error};
    return Response{SimpleOkResponse{"load", command.display ? "preset loaded and displayed" : "preset loaded"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleDelete(const DeleteCommand& command) {
    if (command.name.empty()) return ProtocolError{"delete", "missing_name", "name is required"};
    std::string error;
    if (!presets_.remove(command.name, error)) return ProtocolError{"delete", "delete_failed", error};
    return Response{SimpleOkResponse{"delete", "preset deleted"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleList() const {
    return Response{ListResponse{presets_.list()}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadBegin(ControlSession& session,
                                                                              const UploadBeginCommand& command) {
    auto& upload = session.transfer().upload();
    if (upload.active) {
        return ProtocolError{"upload_begin", "upload_active", "abort or finish the current upload first"};
    }
    if (command.width < 1 || command.width > 512 || command.height < 1 || command.height > 512 ||
        (command.linear && command.height != 1)) {
        return ProtocolError{"upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1"};
    }
    const std::size_t byteCount = (std::size_t(command.width) * command.height + 7U) / 8U;
    if (byteCount > 32768U) {
        return ProtocolError{"upload_begin", "matrix_too_large", "packed matrix exceeds 32768 bytes"};
    }

    upload.active = true;
    upload.width = command.width;
    upload.height = command.height;
    upload.linear = command.linear;
    upload.quiet = std::clamp<uint8_t>(command.quiet, 0, 32);
    upload.rotation = command.rotation;
    upload.invert = command.invert;
    upload.display = command.display;
    upload.label = command.label.empty() ? "Uploaded matrix" : command.label;
    upload.bytes.assign(byteCount, 0U);
    upload.nextOffset = 0;

    return Response{UploadBeginResponse{byteCount, 0}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadChunk(ControlSession& session,
                                                                              const UploadChunkCommand& command) {
    auto& upload = session.transfer().upload();
    if (!upload.active) return ProtocolError{"upload_chunk", "no_upload", "upload_begin is required"};
    if (command.offset != upload.nextOffset) {
        return ProtocolError{"upload_chunk", "unexpected_offset", "chunks must be sequential"};
    }
    if (command.offset + command.data.size() > upload.bytes.size()) {
        return ProtocolError{"upload_chunk", "overflow", "chunk exceeds declared matrix size"};
    }
    std::copy(command.data.begin(), command.data.end(), upload.bytes.begin() + std::ptrdiff_t(command.offset));
    upload.nextOffset += command.data.size();
    return Response{UploadChunkResponse{command.data.size(), upload.nextOffset}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadEnd(ControlSession& session,
                                                                            const UploadEndCommand& command) {
    auto& upload = session.transfer().upload();
    if (!upload.active) return ProtocolError{"upload_end", "no_upload", "upload_begin is required"};
    if (upload.nextOffset != upload.bytes.size()) {
        return ProtocolError{"upload_end", "incomplete", "not all declared bytes were received"};
    }
    const uint32_t actualCrc = crc32(upload.bytes.data(), upload.bytes.size());
    if (command.expectedCrc32.has_value() && *command.expectedCrc32 != actualCrc) {
        return ProtocolError{"upload_end", "crc_mismatch", "uploaded bytes failed CRC32 validation"};
    }

    espbarcode::BitMatrix matrix(upload.width, upload.height);
    matrix.packed() = upload.bytes;
    std::string error;
    const bool success = device_.setUploadedMatrix(std::move(matrix), upload.linear, upload.quiet, upload.rotation,
                                                    upload.invert, upload.label, upload.display, error);
    const bool displayed = upload.display;
    upload.reset();
    if (!success) return ProtocolError{"upload_end", "display_failed", error};
    return Response{UploadEndResponse{actualCrc, displayed}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleUploadAbort(ControlSession& session) {
    session.transfer().upload().reset();
    return Response{SimpleOkResponse{"upload_abort", "upload discarded"}};
}

void ControlProtocolEngine::handleDownload(const DownloadCommand& command, IControlResponseSink& sink) const {
    if (!device_.hasCurrent()) {
        sink.sendError(ProtocolError{"download", "no_symbol", "no current symbol"});
        return;
    }
    const auto& result = device_.currentResult();
    const auto& bytes = result.matrix.packed();
    const std::size_t chunkSize = std::clamp<std::size_t>(command.chunkBytes, 48, 768);
    const uint32_t checksum = crc32(bytes.data(), bytes.size());

    DownloadBeginEvent begin;
    begin.width = result.matrix.width();
    begin.height = result.matrix.height();
    begin.linear = result.linear;
    begin.quiet = device_.currentQuietZone();
    begin.rotation = espbarcode::toString(device_.currentRotation());
    begin.invert = device_.currentInvert();
    begin.label = device_.currentLabel();
    begin.bytes = bytes.size();
    begin.encoding = "base64-packed-msb-first";
    begin.crc32 = checksum;
    sink.send(Response{begin});

    for (std::size_t offset = 0; offset < bytes.size(); offset += chunkSize) {
        const std::size_t count = std::min(chunkSize, bytes.size() - offset);
        DownloadChunkEvent chunk;
        chunk.offset = offset;
        chunk.data.assign(bytes.begin() + std::ptrdiff_t(offset), bytes.begin() + std::ptrdiff_t(offset + count));
        sink.send(Response{chunk});
    }

    DownloadEndEvent end;
    end.bytes = bytes.size();
    end.crc32 = checksum;
    sink.send(Response{end});
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleBacklight(const BacklightCommand& command) {
    deviceControl_.setBacklight(command.on);
    return Response{SimpleOkResponse{"backlight", command.on ? "backlight on" : "backlight off"}};
}

ControlProtocolEngine::CommandResult ControlProtocolEngine::handleReboot() {
    return Response{SimpleOkResponse{"reboot", "rebooting"}};
}

}  // namespace esplink
