#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

#include "ApplicationPorts.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"

namespace esplink {

class SerialLegacyEndpoint : public IControlResponseSink {
public:
    SerialLegacyEndpoint(ControlProtocolEngine& engine, ControlSession& session, IDeviceControl& deviceControl,
                         const IBarcodeDevice& device);

    void begin();
    void loop();
    bool upgradeRequested() const { return upgradeRequested_; }

    // IControlResponseSink
    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

private:
    void processLine(const std::string& line);

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    IDeviceControl& deviceControl_;
    const IBarcodeDevice& device_;
    std::string line_;
    bool discardingLine_ = false;
    JsonObjectConst currentRequest_;  // valid only during a send()/sendError() call from within processLine()
    bool upgradeRequested_ = false;
    uint64_t nextOperationId_ = 1;
};

}  // namespace esplink
