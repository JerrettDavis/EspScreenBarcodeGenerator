#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLECharacteristic.h>
#include <BLEServer.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationPorts.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "Envelope.h"
#include "FrameAssembler.h"
#include "HopFrame.h"

namespace esplink {

// Native BLE GATT carrier endpoint: the display-side compatibility profile
// (`CarrierProfileId::BleGattV1`), a Windows/Linux/macOS-reachable alternative to the
// ESP-NOW endpoint that needs no second ESP32 gateway board. One GATT "command"
// characteristic (write) carries host->device hop frames; one "event" characteristic
// (notify) carries device->host hop frames. Each GATT write/notify *is* one raw hop
// frame — no COBS delimiting, matching EspNowEndpoint's datagram-carrier model (see
// docs/PROTOCOL_V2.md §5/§8) rather than SerialCobsEndpoint's byte-stream model.
//
// `onWrite` (BLECharacteristicCallbacks) and `onConnect`/`onDisconnect`
// (BLEServerCallbacks) run on the BLE host stack's task, not the Arduino loop task:
// `onWrite` only copies bytes into a bounded queue — no parsing, allocation, or engine
// dispatch happens there. `loop()` drains that queue on the main task.
class BleGattEndpoint : public IControlResponseSink, public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
    BleGattEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device);

    // Initializes the BLE stack, creates the service/characteristics, and starts
    // advertising. Returns false and leaves `error` set on failure.
    bool begin(const char* deviceName, std::string& error);

    void loop();

    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

    // BLEServerCallbacks
    void onConnect(BLEServer* server) override;
    void onDisconnect(BLEServer* server) override;

    // BLECharacteristicCallbacks
    void onWrite(BLECharacteristic* characteristic) override;

    bool isConnected() const { return connected_; }

    static constexpr std::size_t kMaxDatagramBytes = 200;  // conservative vs. a requested-but-unguaranteed MTU
    static constexpr std::size_t kRxQueueCapacity = 8;

private:
    struct RxDatagram {
        std::array<uint8_t, kMaxDatagramBytes> bytes{};
        std::size_t length = 0;
    };

    void processDatagram(const RxDatagram& datagram);
    void processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body);
    void sendEnvelope(MessageKind kind, ServiceId serviceId, const std::vector<uint8_t>& bodyBytes,
                      uint64_t correlationId);

    static const char* mapV2Name(const std::string& name);

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    const IBarcodeDevice& device_;
    FrameAssembler assembler_;

    BLEServer* server_ = nullptr;
    BLECharacteristic* commandCharacteristic_ = nullptr;
    BLECharacteristic* eventCharacteristic_ = nullptr;
    volatile bool connected_ = false;

    uint32_t linkMessageCounter_ = 1;
    uint64_t nextResponseOperationId_ = 1;
    uint64_t currentRequestOperationId_ = 0;
    std::string currentRequestName_;

    // Fixed-capacity ring buffer filled by onWrite() (BLE stack task), drained by loop().
    std::array<RxDatagram, kRxQueueCapacity> rxQueue_{};
    volatile std::size_t rxHead_ = 0;
    volatile std::size_t rxTail_ = 0;
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace esplink
