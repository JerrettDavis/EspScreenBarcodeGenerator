#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

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

// Native ESP-NOW carrier endpoint: the display-side compatibility profile described in
// docs/PROTOCOL_V2.md §5/§8 (`CarrierProfileId::EspNowV1`, 250-byte datagram ceiling).
// Each ESP-NOW payload *is* one raw hop frame — no COBS delimiting, since ESP-NOW already
// delivers discrete datagrams. Messages larger than one frame's ~214-byte payload budget
// are split with `fragmentIntoHopFrames` and reassembled with the same `FrameAssembler`
// every other endpoint uses.
//
// `onReceive`/`onSent` are free functions (ESP-NOW's C callback API takes no user context
// pointer) that forward into the single endpoint instance via `instance()`. The receive
// callback runs on the Wi-Fi/ESP-NOW task, not the Arduino loop task: it only copies bytes
// into a bounded, statically-sized queue — no parsing, allocation, or engine dispatch
// happens there. `loop()` drains that queue on the main task.
class EspNowEndpoint : public IControlResponseSink {
public:
    EspNowEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device);

    // Initializes Wi-Fi (station mode, no AP join), esp_now, registers callbacks, and adds
    // the broadcast address as an unencrypted peer. Returns false and leaves `error` set on
    // failure; the caller decides whether that's fatal.
    bool begin(std::string& error);

    void loop();

    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

    // The MAC address this radio is broadcasting from, formatted "AA:BB:CC:DD:EE:FF".
    // Empty until `begin()` succeeds.
    const std::string& macAddress() const { return macAddress_; }

    static constexpr std::size_t kMaxDatagramBytes = 250;    // ESP_NOW_MAX_DATA_LEN
    static constexpr std::size_t kRxQueueCapacity = 8;
    static EspNowEndpoint* instance() { return instance_; }

    // Called from the ESP-NOW receive callback (Wi-Fi task context). Bounded, allocation-free.
    void enqueueReceived(const uint8_t* mac, const uint8_t* data, std::size_t length);

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
    std::string macAddress_;

    uint32_t linkMessageCounter_ = 1;
    uint64_t nextResponseOperationId_ = 1;
    uint64_t currentRequestOperationId_ = 0;
    std::string currentRequestName_;

    // Fixed-capacity ring buffer filled by the ESP-NOW receive callback, drained by loop().
    // A full queue drops the newest datagram rather than growing — bounded memory per
    // docs' "no unbounded reassembly"/"no dynamic allocation from ESP-NOW callbacks" rule.
    std::array<RxDatagram, kRxQueueCapacity> rxQueue_{};
    volatile std::size_t rxHead_ = 0;  // next slot loop() reads
    volatile std::size_t rxTail_ = 0;  // next slot the callback writes
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;

    static EspNowEndpoint* instance_;
};

}  // namespace esplink
