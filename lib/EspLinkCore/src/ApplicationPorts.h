#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "EspBarcodeCore.h"
#include "ScreenOrientation.h"

namespace esplink {

class IBarcodeDevice {
public:
    virtual ~IBarcodeDevice() = default;

    virtual bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) = 0;
    virtual bool setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                                   espbarcode::Rotation rotation, bool invert, const std::string& label,
                                   bool display, std::string& error) = 0;
    virtual bool displayCurrent(std::string& error) = 0;
    virtual void closeBarcode() = 0;
    virtual void showHome(const std::string& status) = 0;

    virtual const espbarcode::BarcodeSpec& activeSpec() const = 0;
    virtual const espbarcode::BarcodeResult& currentResult() const = 0;
    virtual bool hasCurrent() const = 0;
    virtual bool currentIsRaw() const = 0;
    virtual uint8_t currentQuietZone() const = 0;
    virtual espbarcode::Rotation currentRotation() const = 0;
    virtual bool currentInvert() const = 0;
    virtual const std::string& currentLabel() const = 0;
    virtual bool barcodeVisible() const = 0;
    virtual const std::string& statusText() const = 0;
};

class IPresetRepository {
public:
    virtual ~IPresetRepository() = default;

    virtual bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error) = 0;
    virtual bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const = 0;
    virtual bool remove(const std::string& name, std::string& error) = 0;
    virtual std::vector<std::string> list() const = 0;
};

class IDeviceControl {
public:
    virtual ~IDeviceControl() = default;

    virtual void setBacklight(bool on) = 0;
    virtual void setOrientation(OrientationTarget target, ScreenOrientation value) = 0;
    virtual uint32_t freeHeapBytes() const = 0;
    // Triggers the actual restart. Implementations are responsible for flushing
    // any transport-specific stream (e.g. Serial.flush()) before restarting.
    virtual void reboot() = 0;
};

// Snapshot of this board's own ESP-NOW gateway-discovery state, as tracked by whichever
// endpoint owns the radio (normally EspNowEndpoint) — see the "gateway.link.ping"/
// "gateway.link.pong" discovery messages under ServiceId::Gateway. `connected` reflects
// recency only (a gateway broadcast/reply seen within a short timeout), matching the same
// "no pairing handshake" philosophy GatewayStats already uses for the gateway's own peer list.
struct GatewayLinkInfo {
    bool connected = false;
    uint32_t ageMs = 0;    // ms since the last gateway ping/pong was seen; meaningful only if gatewayId is non-empty
    uint32_t rttMs = 0;    // round-trip time of the most recent probe this board sent, if any
    std::string gatewayId; // empty until a gateway has been seen at least once this boot
};

// Implemented by whichever endpoint owns the ESP-NOW radio (EspNowEndpoint in practice) so
// ControlProtocolEngine can surface gateway-discovery state through the ordinary `status`
// command without depending on any Arduino/ESP-NOW header itself (this library stays portable
// and host-testable; EspNowEndpoint, which is not, lives outside it in `src/`).
class IGatewayLinkStatusSource {
public:
    virtual ~IGatewayLinkStatusSource() = default;
    virtual GatewayLinkInfo gatewayLinkStatus() const = 0;
};

}  // namespace esplink
