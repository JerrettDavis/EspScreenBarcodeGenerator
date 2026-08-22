#pragma once

#include "ApplicationPorts.h"
#include "BarcodeApplication.h"

namespace esplink {

class EspIdfDeviceControl : public IDeviceControl {
public:
    explicit EspIdfDeviceControl(BarcodeApplication& application) : application_(application) {}

    void setBacklight(bool on) override { application_.setBacklight(on); }
    void setOrientation(OrientationTarget target, ScreenOrientation value) override {
        application_.setOrientation(target, value);
    }
    uint32_t freeHeapBytes() const override;  // .cpp: return ESP.getFreeHeap();
    void reboot() override;                   // .cpp: ESP.restart();

private:
    BarcodeApplication& application_;
};

}  // namespace esplink
