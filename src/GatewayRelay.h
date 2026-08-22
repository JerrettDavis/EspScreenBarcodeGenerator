#pragma once

#include <Arduino.h>

#include <array>
#include <cstdint>
#include <vector>

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
    void begin();

    void loop();

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

    FrameAssembler usbAssembler_;
    FrameAssembler espNowAssembler_;
    std::vector<uint8_t> rxBlock_;  // USB COBS accumulation buffer
    uint32_t usbToEspNowLinkMessageCounter_ = 1;
    uint32_t espNowToUsbLinkMessageCounter_ = 1;
    GatewayStats linkStats_;

    std::array<RxDatagram, kEspNowRxQueueCapacity> rxQueue_{};
    volatile std::size_t rxHead_ = 0;  // next slot loop() reads
    volatile std::size_t rxTail_ = 0;  // next slot the callback writes
    portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;

    static GatewayRelay* instance_;
};

}  // namespace esplink
