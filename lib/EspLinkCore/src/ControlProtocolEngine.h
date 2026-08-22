#pragma once

#include <string>

#include "ApplicationPorts.h"
#include "ControlSession.h"
#include "ProtocolCommands.h"

namespace esplink {

class IControlResponseSink {
public:
    virtual ~IControlResponseSink() = default;
    virtual void send(const Response& response) = 0;
    virtual void sendError(const ProtocolError& error) = 0;
};

class ControlProtocolEngine {
public:
    ControlProtocolEngine(IBarcodeDevice& device, IPresetRepository& presets,
                          IDeviceControl& deviceControl, std::string firmwareVersion);

    // `transportName` (e.g. "usb-uart-ndjson", "usb-cobs-v2") is embedded verbatim in
    // `hello`'s response and `home`'s internal status text. The engine never branches on it.
    void handle(ControlSession& session, const Command& command, OperationId operationId,
               const char* transportName, IControlResponseSink& sink);

private:
    using CommandResult = ControlSession::CommandResult;

    CommandResult dispatchSingle(ControlSession& session, const Command& command, const char* transportName);

    CommandResult handleHello(const char* transportName) const;
    CommandResult handleCapabilities() const;
    CommandResult handleStatus() const;
    CommandResult handleGenerate(const GenerateCommand& command);
    CommandResult handleDisplay(const DisplayCommand& command);
    CommandResult handleClose();
    CommandResult handleHome(const char* transportName);
    CommandResult handleSave(const SaveCommand& command);
    CommandResult handleLoad(const LoadCommand& command);
    CommandResult handleDelete(const DeleteCommand& command);
    CommandResult handleList() const;
    CommandResult handleUploadBegin(ControlSession& session, const UploadBeginCommand& command);
    CommandResult handleUploadChunk(ControlSession& session, const UploadChunkCommand& command);
    CommandResult handleUploadEnd(ControlSession& session, const UploadEndCommand& command);
    CommandResult handleUploadAbort(ControlSession& session);
    void handleDownload(const DownloadCommand& command, IControlResponseSink& sink) const;
    CommandResult handleBacklight(const BacklightCommand& command);
    CommandResult handleOrientation(const OrientationCommand& command);
    CommandResult handleReboot();

    IBarcodeDevice& device_;
    IPresetRepository& presets_;
    IDeviceControl& deviceControl_;
    std::string firmwareVersion_;
};

const char* commandCatalogName(const Command& command);

}  // namespace esplink
