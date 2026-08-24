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

    // Best-effort external-power heuristic: this board has no dedicated charge-status pin, so
    // BarcodeApplication's inactivity timeout (Settings > Power) picks between the "plugged
    // in" and "on battery" timeout using this instead. A resting/discharging Li-Po does not
    // exceed its ~4.20V full-charge rest voltage; a battery-node reading above that is best
    // explained by an active CC/CV charge current, i.e. USB power present. Header-only (no
    // Arduino dependency) so it's directly unit-testable from the native test build.
    //
    // Known limitation, confirmed on real hardware: once a plugged-in battery finishes bulk
    // charging and the charger drops to trickle/maintenance current, the sensed voltage relaxes
    // back down into the same ~4.15-4.20V band a fully-charged *unplugged* battery rests at --
    // the two are indistinguishable by voltage alone. The threshold is set above that band on
    // purpose: a plugged-in-but-topped-off board is misread as "on battery" (harmless -- it
    // just dims a little sooner than it needs to), rather than the reverse, which would let an
    // unplugged, fully-charged board keep the long timeout and run its battery down unattended.
    static constexpr float kExternalPowerVoltsThreshold = 4.22f;
    static bool likelyExternalPower(float volts) { return volts >= kExternalPowerVoltsThreshold; }

private:
    static uint8_t voltageToPercent(float volts);
};
