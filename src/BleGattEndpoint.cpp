#include "BleGattEndpoint.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEUtils.h>

#include <cstring>

#include "Fragmenter.h"
#include "JsonCommandCodec.h"

namespace esplink {

namespace {
// Fixed UUIDs for the EspLink v2 control service (docs/PROTOCOL_V2.md §5's `BleGattV1`
// profile). Generated once and pinned here and in the .NET `BleGattConnector` — treat as
// a wire constant, not a per-build value.
constexpr const char* kServiceUuid = "6f6d7501-2e73-4a1a-9d3f-1c9b6f4e5a01";
constexpr const char* kCommandCharUuid = "6f6d7502-2e73-4a1a-9d3f-1c9b6f4e5a01";  // write: host -> device
constexpr const char* kEventCharUuid = "6f6d7503-2e73-4a1a-9d3f-1c9b6f4e5a01";    // notify: device -> host
}  // namespace

BleGattEndpoint::BleGattEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device)
    : engine_(engine), session_(session), device_(device) {}

bool BleGattEndpoint::begin(const char* deviceName, std::string& error) {
    BLEDevice::init(deviceName);
    BLEDevice::setMTU(247);  // best-effort; the central may negotiate lower (see header note)

    server_ = BLEDevice::createServer();
    if (server_ == nullptr) {
        error = "BLEDevice::createServer failed";
        return false;
    }
    server_->setCallbacks(this);

    BLEService* service = server_->createService(kServiceUuid);
    if (service == nullptr) {
        error = "createService failed";
        return false;
    }

    commandCharacteristic_ = service->createCharacteristic(
        kCommandCharUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    if (commandCharacteristic_ == nullptr) {
        error = "createCharacteristic (command) failed";
        return false;
    }
    commandCharacteristic_->setCallbacks(this);

    eventCharacteristic_ = service->createCharacteristic(kEventCharUuid, BLECharacteristic::PROPERTY_NOTIFY);
    if (eventCharacteristic_ == nullptr) {
        error = "createCharacteristic (event) failed";
        return false;
    }
    eventCharacteristic_->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();

    return true;
}

void BleGattEndpoint::onConnect(BLEServer* /*server*/) { connected_ = true; }

void BleGattEndpoint::onDisconnect(BLEServer* /*server*/) {
    connected_ = false;
    // The BLE stack stops advertising once connected; resume so another controller can pair.
    BLEDevice::startAdvertising();
}

void BleGattEndpoint::onWrite(BLECharacteristic* characteristic) {
    if (characteristic != commandCharacteristic_) return;
    const uint8_t* data = characteristic->getData();
    const std::size_t length = characteristic->getLength();
    if (data == nullptr || length == 0 || length > kMaxDatagramBytes) return;

    portENTER_CRITICAL(&rxMux_);
    const std::size_t nextTail = (rxTail_ + 1) % kRxQueueCapacity;
    if (nextTail == rxHead_) {
        portEXIT_CRITICAL(&rxMux_);  // queue full: drop the newest datagram
        return;
    }
    RxDatagram& slot = rxQueue_[rxTail_];
    memcpy(slot.bytes.data(), data, length);
    slot.length = length;
    rxTail_ = nextTail;
    portEXIT_CRITICAL(&rxMux_);
}

void BleGattEndpoint::loop() {
    for (;;) {
        portENTER_CRITICAL(&rxMux_);
        if (rxHead_ == rxTail_) {
            portEXIT_CRITICAL(&rxMux_);
            break;
        }
        RxDatagram datagram = rxQueue_[rxHead_];
        rxHead_ = (rxHead_ + 1) % kRxQueueCapacity;
        portEXIT_CRITICAL(&rxMux_);

        processDatagram(datagram);
    }
}

void BleGattEndpoint::processDatagram(const RxDatagram& datagram) {
    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(datagram.bytes.data(), datagram.length, header, payload, frameError)) return;

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = assembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    MessageEnvelope envelope;
    std::vector<uint8_t> body;
    CodecError envelopeError;
    if (!decodeEnvelope(assembled.data(), assembled.size(), envelope, body, envelopeError)) return;

    processMessage(envelope, body);
}

void BleGattEndpoint::processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body) {
    if (envelope.kind != MessageKind::Command) return;  // this endpoint only accepts commands from a controller

    JsonDocument document;
    if (deserializeJson(document, body.data(), body.size())) return;
    JsonObjectConst wrapper = document.as<JsonObjectConst>();
    const char* name = wrapper["name"] | "";
    JsonObjectConst innerBody = wrapper["body"].as<JsonObjectConst>();

    const char* v1Name = mapV2Name(name);
    currentRequestOperationId_ = envelope.operationId;
    currentRequestName_ = name;

    if (v1Name == nullptr) {
        sendError(ProtocolError{name, "unknown_command", "command not supported over EspLink v2 this release"});
        return;
    }

    Command command;
    std::string errorCode, errorMessage;
    if (!JsonCommandCodec::decode(v1Name, innerBody, device_, command, errorCode, errorMessage)) {
        sendError(ProtocolError{name, errorCode, errorMessage});
        return;
    }

    engine_.handle(session_, command, OperationId{envelope.operationId}, "ble-gatt-v1", *this);
}

// Same v2-name -> v1-name subset SerialCobsEndpoint/EspNowEndpoint map (docs/PROTOCOL_V2.md
// §7) — this endpoint drives the identical ControlProtocolEngine handlers over BLE.
const char* BleGattEndpoint::mapV2Name(const std::string& name) {
    if (name == "system.hello" || name == "system.ping") return "hello";
    if (name == "barcode.generate") return "generate";
    if (name == "barcode.close") return "close";
    if (name == "device.backlight.set") return "backlight";
    return nullptr;
}

void BleGattEndpoint::sendEnvelope(MessageKind kind, ServiceId serviceId, const std::vector<uint8_t>& bodyBytes,
                                   uint64_t correlationId) {
    if (!connected_ || eventCharacteristic_ == nullptr) return;

    MessageEnvelope envelope;
    envelope.kind = kind;
    envelope.serviceId = serviceId;
    envelope.codecId = CodecId::Json;
    envelope.controlSessionId = session_.id().value;
    envelope.operationId = nextResponseOperationId_++;
    envelope.correlationId = correlationId;

    std::vector<uint8_t> message;
    CodecError encError;
    if (!encodeEnvelope(envelope, bodyBytes, message, encError)) return;

    HopFrameHeader header;
    header.trafficClass = TrafficClass::Control;
    header.profileId = CarrierProfileId::BleGattV1;
    header.linkSessionId = session_.id().value;
    header.linkMessageId = linkMessageCounter_++;

    std::vector<std::vector<uint8_t>> frames;
    CodecError fragmentError;
    if (!fragmentIntoHopFrames(message.data(), message.size(), header, kMaxDatagramBytes, frames, fragmentError)) {
        return;
    }

    for (const auto& frame : frames) {
        eventCharacteristic_->setValue(const_cast<uint8_t*>(frame.data()), frame.size());
        eventCharacteristic_->notify();
    }
}

void BleGattEndpoint::send(const Response& response) {
    // system.hello gets a distinct response name/body per docs/PROTOCOL_V2.md §7, matching
    // the other v2 endpoints — every other mapped command echoes its own request name.
    const std::string responseName = (currentRequestName_ == "system.hello") ? "system.welcome" : currentRequestName_;

    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = responseName;
    JsonObject body = wrapper["body"].to<JsonObject>();
    if (responseName == "system.welcome") {
        body["deviceId"] = "esbg-ble-gatt-v1";
        body["firmware"] = ESPBARCODE_VERSION;
        body["selectedVersion"] = "2.0";
        body["controlSessionId"] = session_.id().value;
        body["carrier"]["profile"] = "ble-gatt-v1";
        body["carrier"]["maxFrameBytes"] = static_cast<uint32_t>(kMaxDatagramBytes);
    } else {
        JsonCommandCodec::encodeBody(response, body);
    }

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelope(MessageKind::Result, ServiceId::System, bodyBytes, currentRequestOperationId_);
}

void BleGattEndpoint::sendError(const ProtocolError& error) {
    JsonDocument wrapper;
    wrapper["schema"] = "esbg.control/2.0";
    wrapper["name"] = currentRequestName_;
    wrapper["error"]["code"] = error.code;
    wrapper["error"]["message"] = error.message;

    std::string serialized;
    serializeJson(wrapper, serialized);
    const std::vector<uint8_t> bodyBytes(serialized.begin(), serialized.end());
    sendEnvelope(MessageKind::Error, ServiceId::System, bodyBytes, currentRequestOperationId_);
}

}  // namespace esplink
