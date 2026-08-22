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

}  // namespace esplink
