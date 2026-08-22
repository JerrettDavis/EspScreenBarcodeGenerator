#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <cstdint>
#include <string>
#include <vector>

#include "DeviceConfigStore.h"
#include "EspBarcodeCore.h"
#include "GatewayRelay.h"
#include "PresetStore.h"
#include "ScreenOrientation.h"
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
    // Permanently switches the Home screen into gateway-mode layout (a status banner and a
    // button to the live stats screen) -- mirrors GatewayRelay's one-way mode switch in main.cpp.
    void enterGatewayMode();
    void updateGatewayStats(const esplink::GatewayRelay::Stats& stats);
    void setOrientation(esplink::OrientationTarget target, esplink::ScreenOrientation value);
    esplink::ScreenOrientation barcodeOrientation() const { return config_.barcodeOrientation(); }
    esplink::ScreenOrientation editorOrientation() const { return config_.editorOrientation(); }

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
    enum class View : uint8_t { Home, TypePicker, Options, Presets, Settings, Barcode, Gateway };
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
    void rebootDevice();

    void applyOrientationForView(View view);

    void drawHome();
    void drawGatewayHomeBanner();
    void drawTypePicker();
    void drawOptions();
    void drawPresets();
    void drawSettings();
    void drawGateway();
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

    static const std::vector<espbarcode::Symbology>& supportedTypes();
    static std::string displayName(espbarcode::Symbology type);

    TFT_eSPI tft_;
    PresetStore presets_;
    DeviceConfigStore config_;
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
    std::size_t presetPage_ = 0;
    bool presetDeleteMode_ = false;

    bool gatewayModeActive_ = false;
    esplink::GatewayRelay::Stats gatewayStats_{};
    uint32_t gatewayStatsRedrawAt_ = 0;
};
