#include "BatteryMonitor.h"

#include <Arduino.h>

namespace {
constexpr int kBatteryAdcPin = 34;
// The onboard sense network halves the battery voltage before it reaches the ADC pin.
constexpr float kDividerRatio = 2.0f;
constexpr int kSampleCount = 8;

// Piecewise-linear approximation of a single-cell LiPo's resting discharge curve --
// far closer to reality than a straight 3.3-4.2V linear map, which over-reports charge
// for most of the discharge cycle and then craters in the last few percent.
struct CurvePoint {
    float volts;
    uint8_t percent;
};
constexpr CurvePoint kCurve[] = {
    {3.30f, 0}, {3.55f, 5}, {3.65f, 15}, {3.70f, 30}, {3.75f, 45}, {3.80f, 60}, {3.90f, 75}, {4.00f, 88}, {4.20f, 100},
};
constexpr int kCurvePoints = sizeof(kCurve) / sizeof(kCurve[0]);
}  // namespace

void BatteryMonitor::begin() {
    analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);  // full 0-3.3V input range
}

float BatteryMonitor::readVoltageVolts() const {
    uint32_t sumMilliVolts = 0;
    for (int i = 0; i < kSampleCount; ++i) {
        sumMilliVolts += analogReadMilliVolts(kBatteryAdcPin);
    }
    const float pinVolts = static_cast<float>(sumMilliVolts) / kSampleCount / 1000.0f;
    return pinVolts * kDividerRatio;
}

uint8_t BatteryMonitor::readPercent() const { return voltageToPercent(readVoltageVolts()); }

uint8_t BatteryMonitor::voltageToPercent(float volts) {
    if (volts <= kCurve[0].volts) return kCurve[0].percent;
    if (volts >= kCurve[kCurvePoints - 1].volts) return kCurve[kCurvePoints - 1].percent;
    for (int i = 1; i < kCurvePoints; ++i) {
        if (volts <= kCurve[i].volts) {
            const CurvePoint& lo = kCurve[i - 1];
            const CurvePoint& hi = kCurve[i];
            const float t = (volts - lo.volts) / (hi.volts - lo.volts);
            return static_cast<uint8_t>(lo.percent + t * static_cast<float>(hi.percent - lo.percent) + 0.5f);
        }
    }
    return 100;
}
