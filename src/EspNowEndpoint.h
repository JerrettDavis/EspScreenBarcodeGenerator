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
//
// Also owns this board's half of the gateway-discovery ping/pong (ServiceId::Gateway,
// "gateway.link.ping"/"gateway.link.pong", docs/PROTOCOL_V2.md §10): while this endpoint is
// running (i.e. the board has NOT been switched into gateway relay mode), it periodically
// broadcasts a "looking for a gateway" ping if it hasn't heard one recently, and always
// answers any gateway-originated ping with a pong carrying its own identity. See
// GatewayRelay for the gateway-side half of the same exchange.
class EspNowEndpoint : public IControlResponseSink, public IGatewayLinkStatusSource {
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

    // IGatewayLinkStatusSource — surfaced through the ordinary `status` command
    // (see ControlProtocolEngine::setGatewayLinkStatusSource).
    GatewayLinkInfo gatewayLinkStatus() const override;

private:
    struct RxDatagram {
        std::array<uint8_t, kMaxDatagramBytes> bytes{};
        std::size_t length = 0;
    };

    void processDatagram(const RxDatagram& datagram);
    void processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body);
    void sendEnvelope(MessageKind kind, ServiceId serviceId, const std::vector<uint8_t>& bodyBytes,
                      uint64_t correlationId);

    // The gateway-discovery ping/pong side channel — handled independently of the ordinary
    // command dispatch path above (it's Event-kind, not Command, and needs no ControlSession).
    void handleGatewayLinkMessage(JsonObjectConst wrapper);
    void maybeSendGatewayProbe();
    void sendGatewayLinkEvent(const char* eventName, uint32_t echoTs);

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

    // Gateway-discovery state (this board's "client" role) — see the class comment above.
    static constexpr uint32_t kGatewayProbeIntervalMs = 3000;    // while not connected
    static constexpr uint32_t kGatewayKeepaliveIntervalMs = 8000; // while connected, to confirm liveness
    static constexpr uint32_t kGatewayLinkTimeoutMs = 6000;
    uint32_t lastGatewaySeenMs_ = 0;   // millis() of the last gateway ping/pong seen; 0 = never
    uint32_t lastGatewayRttMs_ = 0;
    std::string lastGatewayId_;
    uint32_t lastProbeSentMs_ = 0;
    uint32_t lastProbeTs_ = 0;         // the `ts` this board embedded in its most recent probe

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
