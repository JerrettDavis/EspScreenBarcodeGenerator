#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationPorts.h"
#include "BatteryMonitor.h"
#include "DeviceConfigStore.h"
#include "EspBarcodeCore.h"
#include "GatewayRelay.h"
#include "PresetStore.h"
#include "ScreenOrientation.h"
#include "SdCardStore.h"
#include "Theme.h"
#include "UiRect.h"

class BarcodeApplication {
public:
    bool begin(std::string& error);
    void loop();

    bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error);
    bool setUploadedMatrix(espbarcode::BitMatrix&& matrix,
                           bool linear,
                           uint8_t quietZone,
                           espbarcode::Rotation rotation,
                           bool invert,
                           const std::string& label,
                           bool display,
                           std::string& error);
    bool displayCurrent(std::string& error);
    void closeBarcode();
    void showHome(const std::string& status = {});
    void setBacklight(bool on);
    // Resets the inactivity clock and wakes the backlight if it was off. Called from every
    // input channel that counts as "the device is being used": the digitizer (pollTouch), raw
    // USB serial bytes (main.cpp), and real application commands arriving over any transport
    // (BarcodeApplicationAdapter) -- see pollInactivity() for the other half of this.
    void noteActivity();
    // Permanently switches the Home screen into gateway-mode layout (a status banner and a
    // button to the live stats screen) -- mirrors GatewayRelay's one-way mode switch in main.cpp.
    void enterGatewayMode();
    void updateGatewayStats(const esplink::GatewayRelay::Stats& stats);
    // Fed every loop() iteration while this board is NOT in gateway relay mode (i.e. it's
    // running its own EspNowEndpoint, possibly discovering a nearby gateway) -- see main.cpp.
    void updateGatewayLinkStatus(const esplink::GatewayLinkInfo& status);
    // True (once) if the on-device "Ping Now" button on the Gateway stats screen was tapped
    // since the last call -- main.cpp polls this each loop() iteration, same pattern as
    // SerialLegacyEndpoint::gatewayRequested().
    bool consumeGatewayPingRequest();
    // True (once) if the Settings screen's "Enter Gateway Mode" button was tapped since the last
    // call -- main.cpp polls this alongside SerialLegacyEndpoint::gatewayRequested() to drive the
    // same one-way Legacy->GatewayRelayMode transition, so gateway mode can be entered from the
    // touchscreen as well as from a connected host.
    bool consumeGatewayModeToggleRequest();

    // Secure Pairing (docs/superpowers/specs/2026-08-22-espnow-secure-pairing-design.md) --
    // fed every loop() iteration from whichever of EspNowEndpoint's/GatewayRelay's identical
    // pairing APIs is currently active (see main.cpp).
    void updateTrustPairingStatus(bool discovering, const std::string& peerFingerprint, uint32_t numericCode,
                                  bool committed, bool cancelled);
    // fingerprints/macs are parallel arrays, same order/length -- see GatewayRelay::fingerprintList()/
    // macList() (Task 7) and EspNowEndpoint's identical pair (Task 6).
    void updateGatewayRelayTrustedPeers(const std::vector<std::string>& fingerprints,
                                        const std::vector<std::array<uint8_t, 6>>& macs);
    void updateEspNowTrustedPeers(const std::vector<std::string>& fingerprints,
                                  const std::vector<std::array<uint8_t, 6>>& macs);
    bool consumeTrustPairRequest(std::array<uint8_t, 6>& outTargetMac);  // "Pair new device" tapped
    bool consumeTrustConfirmRequest();
    bool consumeTrustDenyRequest();
    bool consumeTrustForgetRequest(std::string& outFingerprint);
    // Tapped the Secure Pairing switch in Settings -- flips the locally-cached display value
    // immediately (so the switch redraws right away) and sets outValue for main.cpp to push into
    // whichever of EspNowEndpoint's/GatewayRelay's TrustConfigStore is actually active (see
    // main.cpp's loop()); BarcodeApplication has no TrustConfigStore of its own, only this
    // cached display flag.
    bool consumeSecurePairingToggleRequest(bool& outValue);
    void refreshSecurePairingEnabled(bool value) { securePairingEnabled_ = value; }  // reflects a push from main.cpp
    bool securePairingEnabled() const { return securePairingEnabled_; }

    void setOrientation(esplink::OrientationTarget target, esplink::ScreenOrientation value);
    esplink::ScreenOrientation barcodeOrientation() const { return config_.barcodeOrientation(); }
    esplink::ScreenOrientation editorOrientation() const { return config_.editorOrientation(); }

    // Read-only status for main.cpp's boot diagnostic log (mirrors the espnow_ready/ble_ready
    // pattern) -- lets SD/battery presence be confirmed from the serial monitor alone, without
    // needing to read the on-device screen.
    bool sdCardMounted() const { return sdCard_.mounted(); }
    bool presetsUseSd() const { return presetsUseSd_; }
    uint8_t batteryPercent() const { return batteryPercent_; }
    float batteryVoltage() const { return batteryVoltage_; }

    const espbarcode::BarcodeSpec& activeSpec() const { return spec_; }
    const espbarcode::BarcodeResult& currentResult() const { return current_; }
    bool hasCurrent() const { return hasCurrent_; }
    bool currentIsRaw() const { return currentIsRaw_; }
    uint8_t currentQuietZone() const { return currentQuietZone_; }
    espbarcode::Rotation currentRotation() const { return currentRotation_; }
    bool currentInvert() const { return currentInvert_; }
    const std::string& currentLabel() const { return currentLabel_; }
    bool barcodeVisible() const { return view_ == View::Barcode; }
    const std::string& statusText() const { return status_; }
    PresetStore& presets() { return presets_; }

    using Rect = uigeom::Rect;

private:
    enum class View : uint8_t { Home, TypePicker, Options, Presets, Settings, Barcode, Gateway, Trust, Storage, Power };
    enum class KeyboardPage : uint8_t { Upper, Lower, Numeric, Symbols };

    void pollTouch();
    bool readTouch(uint16_t& x, uint16_t& y);
    void handleTouch(uint16_t x, uint16_t y);
    void handleHomeTouch(uint16_t x, uint16_t y);
    void handleTypeTouch(uint16_t x, uint16_t y);
    void handleOptionsTouch(uint16_t x, uint16_t y);
    void handlePresetsTouch(uint16_t x, uint16_t y);
    void handleSettingsTouch(uint16_t x, uint16_t y);
    void handleKeyboardTouch(uint16_t x, uint16_t y);
    void handleGatewayTouch(uint16_t x, uint16_t y);
    void handleTrustTouch(uint16_t x, uint16_t y);
    void handleStorageTouch(uint16_t x, uint16_t y);
    void handlePowerTouch(uint16_t x, uint16_t y);
    void rebootDevice();

    void applyOrientationForView(View view);

    void drawHome();
    void drawGatewayHomeBanner();
    void drawTypePicker();
    void drawOptions();
    void drawPresets();
    void drawSettings();
    // Redraws only the ESP-NOW gateway-link status row within Settings, in place -- used by
    // updateGatewayLinkStatus()'s periodic refresh so it doesn't have to fillScreen() the whole
    // Settings view (and everything on it) just to update this one line.
    void drawSettingsLinkStatusRow();
    void drawGateway();
    void drawTrust();
    void drawStorage();
    // Redraws only the battery-status line within the Storage screen, in place -- same
    // partial-redraw pattern as drawSettingsLinkStatusRow, fed by pollBattery()'s periodic
    // refresh so it doesn't fillScreen() the whole Storage view once per tick.
    void drawStorageBatteryRow();
    // Settings > Power: cycles the plugged-in/on-battery backlight-timeout presets and shows
    // which power state pollInactivity() currently thinks the board is in.
    void drawPower();
    // Turns the backlight off once lastActivityAt_ is older than whichever timeout applies to
    // the current power state (config_.backlightTimeout{PluggedIn,Battery}Sec, picked via
    // BatteryMonitor::likelyExternalPower); a 0 timeout disables auto-dim for that state.
    void pollInactivity();
    // Draws the small battery icon+percent badge at `rect` -- called from drawHome() and
    // drawSubHeader() (so it appears on every screen) as well as pollBattery()'s periodic
    // partial refresh. No-op when config_.showBatteryPercent() is false.
    void drawBatteryBadge(const Rect& rect);
    // Redraws just the battery badge for whichever view is currently on screen, picking
    // the right rect for Home vs. a subHeader-based screen; used by pollBattery().
    void redrawBatteryBadge();
    void pollBattery();
    void drawKeyboard();
    void drawButton(const Rect& rect,
                    const std::string& text,
                    bool selected = false,
                    uint16_t fill = 0);
    void drawStatus();
    void drawDataPreview();
    void appendCharacter(char c);
    void appendText(const std::string& text);
    void backspace();
    void cycleOption(int row, int direction);
    void selectType(std::size_t index);
    bool renderCurrent(std::string& error);
    void setStatus(const std::string& status, bool redraw = true);

    // Theming: the palette is derived from config_.darkTheme() rather than cached, so a toggle
    // just needs a redraw -- there is no separate "current theme" state to keep in sync.
    const uigeom::Theme& theme() const { return uigeom::themeFor(config_.darkTheme()); }
    void toggleTheme();
    void redrawView(View view);
    // Shared chrome for every non-Home screen: a back chevron (optional), a title, and the
    // theme toggle switch. Returns the y-coordinate content should start below.
    int16_t drawSubHeader(const std::string& title, bool showBack = true);
    bool handleSubHeaderTouch(uint16_t x, uint16_t y, View backTarget, bool hasBack = true);
    void drawThemeSwitch(const Rect& rect);
    void drawMiniSwitch(const Rect& rect, bool on);

    static const std::vector<espbarcode::Symbology>& supportedTypes();
    static std::string displayName(espbarcode::Symbology type);
    static std::string symbologyHint(espbarcode::Symbology type);

    TFT_eSPI tft_;
    PresetStore presets_;
    DeviceConfigStore config_;
    SdCardStore sdCard_;
    BatteryMonitor battery_;
    // What PresetStore was actually begun against (SD vs LittleFS) -- decided once, at
    // begin(), from config_.sdCardStorageEnabled() && sdCard_.mounted(). The Storage screen
    // compares this against the live setting to tell the user when a toggle needs a reboot
    // to take effect.
    bool presetsUseSd_ = false;
    uint8_t batteryPercent_ = 100;
    float batteryVoltage_ = 0.0f;
    uint8_t lastDrawnBatteryPercent_ = 255;  // sentinel: forces the first draw to happen
    // Tracks the charging glyph's last-drawn state so pollBattery() also redraws when
    // BatteryMonitor::likelyExternalPower() flips but the percent happens not to change.
    bool lastDrawnBatteryCharging_ = false;
    uint32_t batteryPollAt_ = 0;
    esplink::ScreenOrientation appliedOrientation_ = esplink::ScreenOrientation::Deg90;
    espbarcode::BarcodeSpec spec_;
    espbarcode::BarcodeResult current_;
    bool hasCurrent_ = false;
    bool currentIsRaw_ = false;
    uint8_t currentQuietZone_ = 4;
    espbarcode::Rotation currentRotation_ = espbarcode::Rotation::Auto;
    bool currentInvert_ = false;
    std::string currentLabel_;

    View view_ = View::Home;
    KeyboardPage keyboardPage_ = KeyboardPage::Upper;
    std::string status_;
    uint32_t barcodeShownAt_ = 0;
    uint32_t lastTouchPoll_ = 0;
    bool touchDown_ = false;
    bool backlightOn_ = true;
    uint32_t lastActivityAt_ = 0;
    std::size_t presetPage_ = 0;
    bool presetDeleteMode_ = false;

    bool gatewayModeActive_ = false;
    esplink::GatewayRelay::Stats gatewayStats_{};
    uint32_t gatewayStatsRedrawAt_ = 0;
    bool gatewayPingRequested_ = false;
    bool gatewayModeToggleRequested_ = false;

    // This board's own gateway-discovery state (client role) -- see updateGatewayLinkStatus.
    esplink::GatewayLinkInfo gatewayLinkStatus_{};
    uint32_t gatewayLinkRedrawAt_ = 0;

    // Secure Pairing / Trust screen state -- see updateTrustPairingStatus/consumeTrust*
    // above and main.cpp's loop() for how these are fed from whichever of
    // EspNowEndpoint/GatewayRelay is currently active.
    bool securePairingEnabled_ = false;
    bool securePairingToggleRequested_ = false;
    bool securePairingToggleValue_ = false;
    bool trustPairRequested_ = false;
    std::array<uint8_t, 6> trustPairTargetMac_{};
    bool trustConfirmRequested_ = false;
    bool trustDenyRequested_ = false;
    bool trustForgetRequested_ = false;
    std::string trustForgetFingerprint_;
    bool trustDiscovering_ = false;
    std::string trustPeerFingerprint_;
    uint32_t trustNumericCode_ = 0;
    bool trustCommitted_ = false;
    bool trustCancelled_ = false;

    // One fingerprint + MAC pair per trusted record, zipped together from fingerprintList()/
    // macList() each loop() tick (see updateGatewayRelayTrustedPeers/updateEspNowTrustedPeers)
    // so drawTrust()/handleTrustTouch() can compare a discovered peer's MAC against
    // already-trusted MACs directly, without string-comparing fingerprints.
    struct TrustPeerRow {
        std::string fingerprint;
        std::array<uint8_t, 6> mac{};
    };
    std::vector<TrustPeerRow> gatewayRelayTrustedPeers_;
    std::vector<TrustPeerRow> espNowTrustedPeers_;
};
