#pragma once

#include <cstddef>
#include <cstdint>

namespace app_config {
constexpr uint16_t kScreenWidth = 320;
constexpr uint16_t kScreenHeight = 480;
constexpr uint8_t kBacklightPin = 27;
constexpr uint32_t kSerialBaud = 115200;
constexpr std::size_t kSerialLineLimit = 4096;
constexpr std::size_t kMaxPayloadBytes = 2048;
constexpr uint32_t kTouchCloseGuardMs = 450;
constexpr uint16_t kTouchThreshold = 600;
constexpr uint16_t kTouchCalibration[5] = {275, 3620, 264, 3532, 4};
constexpr const char* kDeviceName = "EspScreenBarcodeGenerator";
constexpr const char* kProtocolVersion = "1.0";
}
