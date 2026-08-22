#pragma once

#include <cstdint>
#include <cstring>

namespace esplink {

// Physical panel rotation, values map 1:1 onto TFT_eSPI's setRotation(0..3).
// Deg90/Deg270 are landscape (width > height); Deg0/Deg180 are portrait.
enum class ScreenOrientation : uint8_t { Deg0 = 0, Deg90 = 1, Deg180 = 2, Deg270 = 3 };

// Which logical screen an orientation setting applies to: the full-screen
// barcode display, or the on-device editor (Home/TypePicker/Options/Presets/
// Settings/keyboard).
enum class OrientationTarget : uint8_t { Barcode = 0, Editor = 1 };

inline bool isLandscape(ScreenOrientation orientation) {
    return orientation == ScreenOrientation::Deg90 || orientation == ScreenOrientation::Deg270;
}

inline bool tryParseScreenOrientation(int value, ScreenOrientation& out) {
    if (value < 0 || value > 3) return false;
    out = static_cast<ScreenOrientation>(value);
    return true;
}

inline bool tryParseOrientationTarget(const char* text, OrientationTarget& out) {
    if (text == nullptr) return false;
    if (std::strcmp(text, "barcode") == 0) {
        out = OrientationTarget::Barcode;
        return true;
    }
    if (std::strcmp(text, "editor") == 0) {
        out = OrientationTarget::Editor;
        return true;
    }
    return false;
}

inline const char* toString(OrientationTarget target) {
    return target == OrientationTarget::Barcode ? "barcode" : "editor";
}

struct RotatedPoint { int x; int y; };

// Maps a touch point already calibrated in the panel's *native* (rotation-0)
// logical coordinate space into the coordinate space of the given live
// rotation. `nativeWidth`/`nativeHeight` are the panel's rotation-0 logical
// dimensions (TFT_WIDTH/TFT_HEIGHT), NOT the current tft.width()/height().
//
// Resistive touch calibration is tied to the touch overlay's physically
// fixed raw axes, not to tft.setRotation() -- so a rotation-0 calibration
// must be re-mapped by hand for the other three rotations. This mirrors the
// MADCTL MX/MY/MV bits ST7796_Rotation.h actually sends the controller for
// each rotation (see BarcodeApplication::readTouch for the derivation).
inline RotatedPoint rotateNativeTouchPoint(int nativeX, int nativeY, ScreenOrientation orientation,
                                           int nativeWidth, int nativeHeight) {
    switch (orientation) {
        case ScreenOrientation::Deg0:
            return {nativeX, nativeY};
        case ScreenOrientation::Deg90:
            return {nativeY, nativeWidth - 1 - nativeX};
        case ScreenOrientation::Deg180:
            return {nativeWidth - 1 - nativeX, nativeHeight - 1 - nativeY};
        case ScreenOrientation::Deg270:
            return {nativeHeight - 1 - nativeY, nativeX};
    }
    return {nativeX, nativeY};
}

}  // namespace esplink
