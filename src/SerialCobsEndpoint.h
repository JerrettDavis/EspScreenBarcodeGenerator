#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationPorts.h"
#include "Cobs.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "Envelope.h"
#include "FrameAssembler.h"
#include "HopFrame.h"

namespace esplink {

class SerialCobsEndpoint : public IControlResponseSink {
public:
    SerialCobsEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device);

    void loop();

    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

private:
    void processCobsBlock(const std::vector<uint8_t>& block);
    void processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body);
    void writeMessage(MessageKind kind, const std::string& name, JsonObjectConst extraBody);
    void writeMessageWithResponse(MessageKind kind, const std::string& name, const Response* response,
                                  const ProtocolError* error);

    static const char* v1NameFor(const std::string& v2Name);  // nullptr if unmapped this session

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    const IBarcodeDevice& device_;
    FrameAssembler assembler_;
    std::vector<uint8_t> rxBlock_;
    uint32_t linkMessageCounter_ = 1;
    uint64_t nextOperationId_ = 1;
    uint64_t currentRequestOperationId_ = 0;
    std::string currentRequestName_;
};

}  // namespace esplink
