#pragma once

#include <string>

#include "ScreenOrientation.h"

// Persists device-wide settings (screen orientation, theme, and the SD-card storage /
// battery-percentage display toggles) to LittleFS, independent of PresetStore's
// per-barcode preset files -- this file itself always lives on internal flash even when
// presets have been redirected to SD, so device settings survive a card that's been
// removed or reformatted.
class DeviceConfigStore {
public:
    bool begin(std::string& error);

    esplink::ScreenOrientation barcodeOrientation() const { return barcodeOrientation_; }
    esplink::ScreenOrientation editorOrientation() const { return editorOrientation_; }
    bool darkTheme() const { return darkTheme_; }
    // Whether presets should be stored on the onboard microSD card instead of internal
    // LittleFS -- see SdCardStore. Takes effect on the next boot (BarcodeApplication picks
    // the backend once, at begin()), not live, since swapping a mounted PresetStore's
    // backend mid-session would strand whichever preset list was already loaded.
    bool sdCardStorageEnabled() const { return sdCardStorageEnabled_; }
    // Whether the battery-percentage badge is drawn on the Home top bar and every
    // subHeader-based screen (see BarcodeApplication::drawBatteryBadge).
    bool showBatteryPercent() const { return showBatteryPercent_; }

    // Screen-off inactivity timeouts (Settings > Power), in seconds -- BarcodeApplication picks
    // between the two each tick based on BatteryMonitor::likelyExternalPower. 0 means "never
    // dim" for that power state. See include/BacklightTimeout.h for the preset ladder these
    // values are drawn from.
    uint32_t backlightTimeoutPluggedInSec() const { return backlightTimeoutPluggedInSec_; }
    uint32_t backlightTimeoutBatterySec() const { return backlightTimeoutBatterySec_; }

    // Updates the in-memory value and persists it; returns false (leaving the
    // stored value unchanged) only if the write fails.
    bool setOrientation(esplink::OrientationTarget target, esplink::ScreenOrientation value, std::string& error);
    bool setDarkTheme(bool value, std::string& error);
    bool setSdCardStorageEnabled(bool value, std::string& error);
    bool setShowBatteryPercent(bool value, std::string& error);
    bool setBacklightTimeoutPluggedInSec(uint32_t value, std::string& error);
    bool setBacklightTimeoutBatterySec(uint32_t value, std::string& error);

private:
    bool load();
    bool save(std::string& error) const;

    esplink::ScreenOrientation barcodeOrientation_ = esplink::ScreenOrientation::Deg90;
    esplink::ScreenOrientation editorOrientation_ = esplink::ScreenOrientation::Deg90;
    bool darkTheme_ = true;
    bool sdCardStorageEnabled_ = false;
    bool showBatteryPercent_ = true;
    uint32_t backlightTimeoutPluggedInSec_ = 300;
    uint32_t backlightTimeoutBatterySec_ = 30;
};
