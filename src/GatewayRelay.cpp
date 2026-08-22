#include "GatewayRelay.h"

#include <esp_now.h>

#include <cstring>

#include "Cobs.h"
#include "Fragmenter.h"
#include "GatewayBridge.h"

namespace esplink {

namespace {
// Must match the broadcast peer EspNowEndpoint::begin() already added -- GatewayRelay reuses
// that radio/peer registration and only takes over the receive callback.
const uint8_t kBroadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void onGatewayEspNowReceive(const uint8_t* mac, const uint8_t* data, int length) {
    if (GatewayRelay* self = GatewayRelay::instance()) {
        if (length > 0) self->enqueueFromEspNow(mac, data, static_cast<std::size_t>(length));
    }
}
}  // namespace

GatewayRelay* GatewayRelay::instance_ = nullptr;

GatewayRelay::GatewayRelay() { instance_ = this; }

void GatewayRelay::begin() {
    rxBlock_.reserve(256);
    esp_now_register_recv_cb(onGatewayEspNowReceive);
}

void GatewayRelay::loop() {
    pollUsb();
    drainEspNowQueue();
}

void GatewayRelay::pollUsb() {
    while (Serial.available() > 0) {
        const uint8_t b = static_cast<uint8_t>(Serial.read());
        if (b == 0x00) {
            if (!rxBlock_.empty()) processUsbCobsBlock(rxBlock_);
            rxBlock_.clear();
            continue;
        }
        if (rxBlock_.size() < 4096) rxBlock_.push_back(b);  // bounded; oversized blocks are dropped at the delimiter
    }
}

void GatewayRelay::processUsbCobsBlock(const std::vector<uint8_t>& block) {
    std::vector<uint8_t> raw;
    if (!cobsDecode(block.data(), block.size(), raw)) return;  // malformed block: drop and resync on the next 0x00

    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(raw.data(), raw.size(), header, payload, frameError)) return;

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = usbAssembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    relayToEspNow(header, assembled);
}

void GatewayRelay::enqueueFromEspNow(const uint8_t* mac, const uint8_t* data, std::size_t length) {
    (void)mac;  // this relay accepts any sender this session, same as EspNowEndpoint
    if (length > kEspNowMaxDatagramBytes) return;  // cannot happen from a real ESP-NOW radio; defensive only

    portENTER_CRITICAL(&rxMux_);
    const std::size_t nextTail = (rxTail_ + 1) % kEspNowRxQueueCapacity;
    if (nextTail == rxHead_) {
        // Queue full: drop the newest datagram rather than growing unboundedly.
        portEXIT_CRITICAL(&rxMux_);
        return;
    }
    RxDatagram& slot = rxQueue_[rxTail_];
    memcpy(slot.bytes.data(), data, length);
    slot.length = length;
    rxTail_ = nextTail;
    portEXIT_CRITICAL(&rxMux_);
}

void GatewayRelay::drainEspNowQueue() {
    for (;;) {
        portENTER_CRITICAL(&rxMux_);
        if (rxHead_ == rxTail_) {
            portEXIT_CRITICAL(&rxMux_);
            break;
        }
        RxDatagram datagram = rxQueue_[rxHead_];
        rxHead_ = (rxHead_ + 1) % kEspNowRxQueueCapacity;
        portEXIT_CRITICAL(&rxMux_);

        processEspNowDatagram(datagram);
    }
}

void GatewayRelay::processEspNowDatagram(const RxDatagram& datagram) {
    HopFrameHeader header;
    std::vector<uint8_t> payload;
    CodecError frameError;
    if (!decodeHopFrame(datagram.bytes.data(), datagram.length, header, payload, frameError)) return;

    std::vector<uint8_t> assembled;
    const AssemblyOutcome outcome = espNowAssembler_.addFragment(header, payload, assembled);
    if (outcome != AssemblyOutcome::Complete) return;

    relayToUsb(header, assembled);
}

void GatewayRelay::relayToEspNow(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled) {
    const HopFrameHeader outHeader =
        relayHeaderFor(sourceHeader, CarrierProfileId::EspNowV1, usbToEspNowLinkMessageCounter_++);
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    if (!fragmentIntoHopFrames(assembled.data(), assembled.size(), outHeader, kEspNowMaxDatagramBytes, frames,
                               error)) {
        return;
    }
    for (const auto& frame : frames) esp_now_send(kBroadcastAddress, frame.data(), frame.size());
}

void GatewayRelay::relayToUsb(const HopFrameHeader& sourceHeader, const std::vector<uint8_t>& assembled) {
    const HopFrameHeader outHeader =
        relayHeaderFor(sourceHeader, CarrierProfileId::StreamStandard, espNowToUsbLinkMessageCounter_++);
    std::vector<std::vector<uint8_t>> frames;
    CodecError error;
    if (!fragmentIntoHopFrames(assembled.data(), assembled.size(), outHeader, kUsbMaxFrameBytes, frames, error)) {
        return;
    }
    for (const auto& frame : frames) sendUsbFrame(frame);
}

void GatewayRelay::sendUsbFrame(const std::vector<uint8_t>& frame) {
    const std::vector<uint8_t> cobs = cobsEncode(frame.data(), frame.size());
    Serial.write(cobs.data(), cobs.size());
    Serial.write(static_cast<uint8_t>(0x00));
}

}  // namespace esplink
