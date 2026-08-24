#pragma once

#include <cstdint>

// Reads the board's battery-voltage sense pin (GPIO34, wired through a 2:1 resistor
// divider ahead of the ESP32 ADC -- the Hosyond/lcdwiki ESP32-32E board family's onboard
// battery monitor; there's no fuel-gauge IC involved) and converts it to an approximate
// charge percentage via a LiPo discharge-curve lookup.
class BatteryMonitor {
public:
    void begin();
    // Averages a few samples to smooth ADC noise; safe to call at whatever cadence the
    // caller wants to poll at (BarcodeApplication throttles this to a few seconds).
    float readVoltageVolts() const;
    uint8_t readPercent() const;

private:
    static uint8_t voltageToPercent(float volts);
};
