#include "EspIdfDeviceControl.h"

#include <Arduino.h>

namespace esplink {

uint32_t EspIdfDeviceControl::freeHeapBytes() const {
    return ESP.getFreeHeap();
}

void EspIdfDeviceControl::reboot() {
    Serial.flush();
    delay(100);
    ESP.restart();
}

}  // namespace esplink
