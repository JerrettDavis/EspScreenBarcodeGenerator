#pragma once

#include <string>

#include "ScreenOrientation.h"

// Persists device-wide settings (currently just screen orientation) to
// LittleFS, independent of PresetStore's per-barcode preset files.
class DeviceConfigStore {
public:
    bool begin(std::string& error);

    esplink::ScreenOrientation barcodeOrientation() const { return barcodeOrientation_; }
    esplink::ScreenOrientation editorOrientation() const { return editorOrientation_; }
    bool darkTheme() const { return darkTheme_; }

    // Updates the in-memory value and persists it; returns false (leaving the
    // stored value unchanged) only if the write fails.
    bool setOrientation(esplink::OrientationTarget target, esplink::ScreenOrientation value, std::string& error);
    bool setDarkTheme(bool value, std::string& error);

private:
    bool load();
    bool save(std::string& error) const;

    esplink::ScreenOrientation barcodeOrientation_ = esplink::ScreenOrientation::Deg90;
    esplink::ScreenOrientation editorOrientation_ = esplink::ScreenOrientation::Deg90;
    bool darkTheme_ = true;
};
