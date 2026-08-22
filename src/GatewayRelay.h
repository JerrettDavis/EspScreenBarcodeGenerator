#pragma once

#include <Arduino.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Envelope.h"
#include "FrameAssembler.h"
#include "GatewayStats.h"
#include "HopFrame.h"

namespace esplink {

// Pure Layer-2 (hop-frame) bridge between the USB COBS carrier and ESP-NOW, activated once a
// host sends the `gateway` command over the legacy USB line (see SerialLegacyEndpoint). Unlike
// every other endpoint in this firmware, GatewayRelay never touches ControlProtocolEngine,
// JsonCommandCodec, or MessageEnvelope's parsed fields -- it reassembles whichever carrier's
// frames arrive, then re-fragments the same opaque envelope bytes for the other carrier's
// frame-size ceiling (docs/PROTOCOL_V2.md §10). It reuses the ESP-NOW radio EspNowEndpoint
// already initialized in setup() -- begin() only takes over the receive callback; main.cpp is
// responsible for no longer calling EspNowEndpoint::loop() once gateway mode is active.
class GatewayRelay {
public:
    // Takes over the ESP-NOW receive callback. The radio itself (WiFi mode, channel,
    // broadcast peer) must already be initialized by EspNowEndpoint::begin().
    // `deviceId` (this board's own ESP-NOW MAC) is embedded in outbound discovery pings/pongs
    // so a browser/host asking `gateway.peers.list` can distinguish the gateway from its peers.
    void begin(std::string deviceId);

    void loop();

    // Broadcasts a fresh "gateway.link.ping" discovery datagram immediately, resetting the
    // periodic interval. Safe to call whether or not begin() has run yet (no-ops).
    void pingNow();

    // Called from the ESP-NOW receive callback (Wi-Fi task context). Bounded, allocation-free
    // -- same contract as EspNowEndpoint::enqueueReceived.
    void enqueueFromEspNow(const uint8_t* mac, const uint8_t* data, std::size_t length);

    static constexpr std::size_t kEspNowMaxDatagramBytes = 250;  // ESP_NOW_MAX_DATA_LEN
    static constexpr std::size_t kUsbMaxFrameBytes = 4096;       // matches app_config::kSerialLineLimit
    static constexpr std::size_t kEspNowRxQueueCapacity = 8;

    static GatewayRelay* instance() { return instance_; }

    GatewayRelay();

    // Live monitoring snapshot for the on-device gateway stats screen (see BarcodeApplication's
    // View::Gateway). usbToEspNowMessageCount/espNowToUsbMessageCount are the number of relayed
    // messages sent so far on each leg.
    struct Stats {
        GatewayStats::Snapshot linkStats;
        uint32_t usbToEspNowMessageCount = 0;
        uint32_t espNowToUsbMessageCount = 0;
    };
    Stats stats() const;

private:
    struct RxDatagram {
        std::array<uint8_t, kEspNowMaxDatagramBytes> bytes{};
        std::array<uint8_t, 6> mac{};
        std::size_t length = 0;
    };

    void pollUsb();
    void drainEspNowQueue();
    void processUsbCobsBlock(const std::vector<uint8_t>& block);
    void processEspNowDatagram(const RxDatagram& datagram);
    void relayToEspNow(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled);
    void relayToUsb(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled);
    void sendUsbFrame(const std::vector<uint8_t>& frame);

    // Gateway-local (ServiceId::Gateway) message handling — deliberately NOT part of the
    // pure-relay path above. `handleGatewayServiceFromUsb` answers the host directly instead
    // of forwarding to ESP-NOW (gateway.peers.list, gateway.ping.now); `handleGatewayServiceFromEspNow`
    // consumes a discovery pong from a peer instead of forwarding it on to the host, since
    // it's this board's own traffic, not something the host's session should see.
    // Both return true if the message was gateway-local (i.e. already fully handled).
    bool handleGatewayServiceFromUsb(const HopFrameHeader& sourceHeader, const MessageEnvelope& envelope,
                                     const std::vector<uint8_t>& jsonBody);
    bool handleGatewayServiceFromEspNow(const MessageEnvelope& envelope, const std::vector<uint8_t>& jsonBody,
                                        const std::array<uint8_t, 6>& fromMac);
    // Broadcasts a "gateway.link.ping"/"gateway.link.pong" Event over ESP-NOW. echoTs is 0 for
    // an unsolicited ping; for a pong it's the peer's original ping ts, echoed back so the
    // recipient can compute RTT.
    void sendGatewayLinkFrame(const char* name, uint32_t echoTs);
    void sendUsbGatewayResponse(const HopFrameHeader& requestHeader, const MessageEnvelope& requestEnvelope,
                               const std::vector<uint8_t>& bodyBytes);

    FrameAssembler usbAssembler_;
    FrameAssembler espNowAssembler_;
    std::vector<uint8_t> rxBlock_;  // USB COBS accumulation buffer
    uint32_t usbToEspNowLinkMessageCounter_ = 1;
    uint32_t espNowToUsbLinkMessageCounter_ = 1;
    uint32_t gatewayLocalLinkMessageCounter_ = 1;  // discovery ping/pong + local gateway.* replies
    GatewayStats linkStats_;
    std::string deviceId_;
    uint32_t lastDiscoveryPingMs_ = 0;
    static constexpr uint32_t kDiscoveryPingIntervalMs = 2000;

    std::array<RxDatagram, kEspNowRxQueueCapacity> rxQueue_{};
    volatile std::size_t rxHead_ = 0;  // next slot loop() reads
    volatile std::size_t rxTail_ = 0;  // next slot the callback writes
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;

    static GatewayRelay* instance_;
};

}  // namespace esplink
