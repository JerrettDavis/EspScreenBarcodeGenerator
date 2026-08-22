#include "BarcodeApplication.h"

#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "RandomPayload.h"
#include "app_config.h"

using namespace espbarcode;
using esplink::OrientationTarget;
using esplink::ScreenOrientation;

namespace {
constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kPanel = 0x2104;
constexpr uint16_t kPanelAlt = 0x3186;
constexpr uint16_t kAccent = 0x05FF;
constexpr uint16_t kAccentDark = 0x0355;
constexpr uint16_t kWarning = 0xFD20;
constexpr uint16_t kDanger = 0xF904;
constexpr uint16_t kText = TFT_WHITE;
constexpr uint16_t kMuted = 0xBDF7;
// Extra hit-test tolerance for the home screen's small, tightly-packed
// buttons (e.g. CLEAR is only 70px wide with 8px gaps on either side).
constexpr int16_t kTouchPad = 4;

using Rect = uigeom::Rect;

std::string clipped(const std::string& value, std::size_t max) {
    if (value.size() <= max) return value;
    return value.substr(0, max - 3) + "...";
}

std::string randomPayloadFor(Symbology type) {
    return randomValidPayload(type, [] { return static_cast<uint32_t>(esp_random()); });
}

std::string printablePreview(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (c == 29) out += "<GS>";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32 || c == 127) out += '.';
        else out.push_back(static_cast<char>(c));
    }
    return out;
}

const char* orientationLabel(ScreenOrientation orientation) {
    switch (orientation) {
        case ScreenOrientation::Deg0: return "0";
        case ScreenOrientation::Deg90: return "90";
        case ScreenOrientation::Deg180: return "180";
        case ScreenOrientation::Deg270: return "270";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Editor screen layout. The editor (Home/TypePicker/Options/Presets/Settings/
// keyboard) is user-orientable independent of the barcode display, so every
// rect below is derived from the *current* screen dimensions rather than a
// fixed 320x480 assumption. Portrait (width <= height) numbers reproduce the
// original fixed layout exactly; landscape numbers are a fresh compact
// layout sized for the shorter 480x320 footprint.
// ---------------------------------------------------------------------------

// Evenly distributes `count` buttons across [margin, width-margin] at row y/h.
std::vector<Rect> distributeRow(uint16_t width, int16_t y, int16_t h, int count,
                                int16_t margin = 8, int16_t gap = 8) {
    std::vector<Rect> rects;
    rects.reserve(static_cast<std::size_t>(count));
    const int totalGap = gap * (count - 1);
    const int buttonWidth = (static_cast<int>(width) - 2 * margin - totalGap) / count;
    int16_t x = margin;
    for (int i = 0; i < count; ++i) {
        rects.push_back(Rect{x, y, static_cast<int16_t>(buttonWidth), h});
        x = static_cast<int16_t>(x + buttonWidth + gap);
    }
    return rects;
}

Rect homeTitleBarRect(uint16_t width, uint16_t height) {
    return Rect{0, 0, static_cast<int16_t>(width), static_cast<int16_t>(height <= 320 ? 22 : 30)};
}

std::array<Rect, 3> homeTopRow(uint16_t width, uint16_t height) {
    const bool wide = width > height;
    const int16_t y = static_cast<int16_t>(wide ? 32 : 38);
    const int16_t h = static_cast<int16_t>(wide ? 30 : 38);
    if (!wide) return {Rect{8, y, 150, h}, Rect{166, y, 70, h}, Rect{244, y, 68, h}};
    return {Rect{8, y, 260, h}, Rect{276, y, 96, h}, Rect{380, y, 88, h}};
}

Rect homeDataBoxRect(uint16_t width, uint16_t height) {
    if (width <= height) return Rect{8, 82, 304, 88};
    return Rect{8, 70, 460, 54};
}

std::vector<Rect> homeActionRow(uint16_t width, uint16_t height) {
    // OPTIONS / PRESETS / DISPLAY / RANDOM / SETTINGS
    const Rect dataBox = homeDataBoxRect(width, height);
    const int16_t y = static_cast<int16_t>(dataBox.y + dataBox.h + 6);
    const int16_t h = static_cast<int16_t>(width > height ? 32 : 38);
    return distributeRow(width, y, h, 5);
}

Rect homeStatusRect(uint16_t width, uint16_t height) {
    const auto row = homeActionRow(width, height);
    const int16_t y = static_cast<int16_t>(row.front().y + row.front().h + 3);
    const int16_t h = static_cast<int16_t>(width > height ? 14 : 17);
    return Rect{8, y, static_cast<int16_t>(width - 16), h};
}

// Gateway mode replaces the normal Home layout entirely (editing the barcode locally has no
// purpose once this board is a pure USB<->ESP-NOW relay), so it gets its own single big button
// rather than fighting the editor's action row for space.
Rect homeGatewayButtonRect(uint16_t width, uint16_t height) {
    const Rect title = homeTitleBarRect(width, height);
    return Rect{8, static_cast<int16_t>(title.h + 28), static_cast<int16_t>(width - 16), 56};
}

struct KeyboardMetrics {
    int16_t top;
    int16_t rowHeight;
};

KeyboardMetrics keyboardMetrics(uint16_t width, uint16_t height) {
    const Rect status = homeStatusRect(width, height);
    const int16_t top = static_cast<int16_t>(status.y + status.h + 4);
    const int16_t rowHeight = static_cast<int16_t>((height - top) / 5);
    return {top, rowHeight};
}

struct ControlRowLayout {
    Rect keys[5];  // FNC1/abc, SYM, SPACE, BKSP, GO
};

ControlRowLayout controlRowLayout(uint16_t width, int16_t y, int16_t h) {
    const int budget = static_cast<int>(width) - 6;  // 1px margins + 4x1px gaps
    const int unit = budget / 6;                     // units: 1, 1, 2, 1, 1
    const int widths[5] = {unit, unit, unit * 2, unit, budget - unit * 5};
    ControlRowLayout layout{};
    int16_t x = 1;
    for (int i = 0; i < 5; ++i) {
        layout.keys[i] = Rect{x, y, static_cast<int16_t>(widths[i]), h};
        x = static_cast<int16_t>(x + widths[i] + 1);
    }
    return layout;
}

}  // namespace

const std::vector<Symbology>& BarcodeApplication::supportedTypes() {
    static const std::vector<Symbology> values = {
        Symbology::QrCode, Symbology::DataMatrix, Symbology::Aztec,
        Symbology::Code128, Symbology::Gs1_128, Symbology::Code39,
        Symbology::UpcA, Symbology::Ean13, Symbology::Ean8,
        Symbology::Itf, Symbology::Itf14, Symbology::Codabar,
        Symbology::Msi
    };
    return values;
}

std::string BarcodeApplication::displayName(Symbology type) {
    switch (type) {
        case Symbology::QrCode: return "QR Code";
        case Symbology::DataMatrix: return "Data Matrix";
        case Symbology::Aztec: return "Aztec";
        case Symbology::Code128: return "Code 128";
        case Symbology::Gs1_128: return "GS1-128";
        case Symbology::Code39: return "Code 39";
        case Symbology::Ean13: return "EAN-13";
        case Symbology::Ean8: return "EAN-8";
        case Symbology::UpcA: return "UPC-A";
        case Symbology::Itf: return "ITF";
        case Symbology::Itf14: return "ITF-14";
        case Symbology::Codabar: return "Codabar";
        case Symbology::Msi: return "MSI";
    }
    return "Unknown";
}

bool BarcodeApplication::begin(std::string& error) {
    pinMode(app_config::kBacklightPin, OUTPUT);
    digitalWrite(app_config::kBacklightPin, HIGH);

    tft_.init();
    if (!config_.begin(error)) return false;
    appliedOrientation_ = config_.editorOrientation();
    tft_.setRotation(static_cast<uint8_t>(appliedOrientation_));
    tft_.setTouch(const_cast<uint16_t*>(app_config::kTouchCalibration));
    tft_.setTextWrap(false, false);
    tft_.fillScreen(kBackground);

    if (!presets_.begin(error)) return false;
    spec_.type = Symbology::QrCode;
    spec_.data = "LAB-TEST-001";
    showHome("Ready: touch keys or use USB serial");
    return true;
}

void BarcodeApplication::loop() {
    pollTouch();
}

void BarcodeApplication::setBacklight(bool on) {
    digitalWrite(app_config::kBacklightPin, on ? HIGH : LOW);
}

void BarcodeApplication::setOrientation(OrientationTarget target, ScreenOrientation value) {
    std::string error;
    if (!config_.setOrientation(target, value, error)) {
        setStatus("Orientation save failed: " + error, view_ == View::Home);
        return;
    }
    if (view_ == View::Barcode) {
        if (target == OrientationTarget::Barcode) {
            std::string renderError;
            displayCurrent(renderError);
        }
        return;
    }
    switch (view_) {
        case View::Home: drawHome(); break;
        case View::TypePicker: drawTypePicker(); break;
        case View::Options: drawOptions(); break;
        case View::Presets: drawPresets(); break;
        case View::Settings: drawSettings(); break;
        case View::Gateway: drawGateway(); break;
        case View::Barcode: break;
    }
}

void BarcodeApplication::applyOrientationForView(View view) {
    const ScreenOrientation desired = (view == View::Barcode) ? config_.barcodeOrientation()
                                                                : config_.editorOrientation();
    if (desired != appliedOrientation_) {
        tft_.setRotation(static_cast<uint8_t>(desired));
        appliedOrientation_ = desired;
    }
}

void BarcodeApplication::pollTouch() {
    const uint32_t now = millis();
    if (now - lastTouchPoll_ < 20) return;
    lastTouchPoll_ = now;

    uint16_t x = 0;
    uint16_t y = 0;
    const bool down = readTouch(x, y);
    if (down && !touchDown_) handleTouch(x, y);
    touchDown_ = down;
}

bool BarcodeApplication::readTouch(uint16_t& outX, uint16_t& outY) {
    // TFT_eSPI's own getTouch()/convertRawXY() scale raw ADC samples by the
    // *live* _width/_height, which track tft_.setRotation() -- but the
    // resistive touch overlay's raw axes are physically fixed to the panel
    // and never rotate. That combination is only correct at the rotation
    // app_config::kTouchCalibration was captured at (0), so we calibrate
    // into that fixed native (rotation-0) space ourselves -- replicating
    // TFT_eSPI::convertRawXY's formula, but against the panel's native
    // TFT_WIDTH/TFT_HEIGHT rather than the current rotated dimensions --
    // then re-map into whichever rotation is currently applied via
    // rotateNativeTouchPoint (see ScreenOrientation.h for the derivation).
    constexpr int kNativeWidth = app_config::kScreenWidth;    // TFT_WIDTH: the panel's native (rotation-0) width
    constexpr int kNativeHeight = app_config::kScreenHeight;  // TFT_HEIGHT: the panel's native (rotation-0) height

    if (tft_.getTouchRawZ() <= app_config::kTouchThreshold) return false;
    uint16_t rawX1 = 0, rawY1 = 0;
    tft_.getTouchRaw(&rawX1, &rawY1);
    if (tft_.getTouchRawZ() <= app_config::kTouchThreshold) return false;
    uint16_t rawX2 = 0, rawY2 = 0;
    tft_.getTouchRaw(&rawX2, &rawY2);
    if (std::abs(static_cast<int>(rawX1) - static_cast<int>(rawX2)) > 20) return false;
    if (std::abs(static_cast<int>(rawY1) - static_cast<int>(rawY2)) > 20) return false;

    const int x0 = app_config::kTouchCalibration[0];
    const int xRange = std::max<int>(1, app_config::kTouchCalibration[1]);
    const int y0 = app_config::kTouchCalibration[2];
    const int yRange = std::max<int>(1, app_config::kTouchCalibration[3]);
    const uint8_t flags = static_cast<uint8_t>(app_config::kTouchCalibration[4]);
    const bool rotate = flags & 0x01;
    const bool invertX = flags & 0x02;
    const bool invertY = flags & 0x04;

    const int calX = rotate ? rawY1 : rawX1;
    const int calY = rotate ? rawX1 : rawY1;
    int nativeX = (calX - x0) * kNativeWidth / xRange;
    int nativeY = (calY - y0) * kNativeHeight / yRange;
    if (invertX) nativeX = kNativeWidth - nativeX;
    if (invertY) nativeY = kNativeHeight - nativeY;
    nativeX = std::clamp(nativeX, 0, kNativeWidth - 1);
    nativeY = std::clamp(nativeY, 0, kNativeHeight - 1);

    const esplink::RotatedPoint rotated =
        esplink::rotateNativeTouchPoint(nativeX, nativeY, appliedOrientation_, kNativeWidth, kNativeHeight);
    if (rotated.x < 0 || rotated.y < 0 || rotated.x >= tft_.width() || rotated.y >= tft_.height()) return false;

    outX = static_cast<uint16_t>(rotated.x);
    outY = static_cast<uint16_t>(rotated.y);
    return true;
}

void BarcodeApplication::handleTouch(uint16_t x, uint16_t y) {
    switch (view_) {
        case View::Home: handleHomeTouch(x, y); break;
        case View::TypePicker: handleTypeTouch(x, y); break;
        case View::Options: handleOptionsTouch(x, y); break;
        case View::Presets: handlePresetsTouch(x, y); break;
        case View::Settings: handleSettingsTouch(x, y); break;
        case View::Gateway: handleGatewayTouch(x, y); break;
        case View::Barcode:
            if (millis() - barcodeShownAt_ >= app_config::kTouchCloseGuardMs) closeBarcode();
            break;
    }
}

void BarcodeApplication::drawButton(const Rect& rect,
                                    const std::string& text,
                                    bool selected,
                                    uint16_t fill) {
    if (fill == 0) fill = selected ? kAccentDark : kPanel;
    tft_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, fill);
    tft_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, selected ? kAccent : kPanelAlt);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, fill);
    tft_.drawString(clipped(text, 19).c_str(), rect.x + rect.w / 2, rect.y + rect.h / 2, 2);
}

void BarcodeApplication::showHome(const std::string& status) {
    view_ = View::Home;
    if (!status.empty()) status_ = status;
    drawHome();
}

void BarcodeApplication::enterGatewayMode() {
    gatewayModeActive_ = true;
    showHome("Gateway mode active: relaying USB <-> ESP-NOW");
}

void BarcodeApplication::updateGatewayStats(const esplink::GatewayRelay::Stats& stats) {
    gatewayStats_ = stats;
    if (view_ != View::Gateway) return;
    const uint32_t now = millis();
    if (now - gatewayStatsRedrawAt_ < 1000) return;
    gatewayStatsRedrawAt_ = now;
    drawGateway();
}

void BarcodeApplication::drawHome() {
    applyOrientationForView(View::Home);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();

    tft_.fillScreen(kBackground);
    const Rect title = homeTitleBarRect(width, height);
    tft_.fillRect(title.x, title.y, title.w, title.h, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString(gatewayModeActive_ ? "GATEWAY MODE" : "ESP BARCODE LAB", width / 2, title.h / 2, 4);

    if (gatewayModeActive_) {
        drawGatewayHomeBanner();
        return;
    }

    const auto topRow = homeTopRow(width, height);
    drawButton(topRow[0], displayName(spec_.type), true);
    drawButton(topRow[1], "CLEAR", false, kDanger);
    drawButton(topRow[2], "SAVE", false, kPanelAlt);

    const Rect dataBox = homeDataBoxRect(width, height);
    tft_.fillRoundRect(dataBox.x, dataBox.y, dataBox.w, dataBox.h, 5, TFT_WHITE);
    tft_.drawRoundRect(dataBox.x, dataBox.y, dataBox.w, dataBox.h, 5, kAccent);
    drawDataPreview();

    const auto actionRow = homeActionRow(width, height);
    drawButton(actionRow[0], "OPTIONS");
    drawButton(actionRow[1], "PRESETS");
    drawButton(actionRow[2], "DISPLAY", true, kAccentDark);
    drawButton(actionRow[3], "RANDOM", false, kWarning);
    drawButton(actionRow[4], "SETTINGS", false, kPanel);
    drawStatus();
    drawKeyboard();
}

void BarcodeApplication::drawGatewayHomeBanner() {
    const uint16_t width = tft_.width();
    const Rect title = homeTitleBarRect(width, tft_.height());

    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(kMuted, kBackground);
    const std::size_t maxChars = static_cast<std::size_t>(std::max(16, (width - 16) / 6));
    tft_.drawString(clipped(status_, maxChars).c_str(), 8, title.h + 10, 1);

    drawButton(homeGatewayButtonRect(width, tft_.height()), "VIEW GATEWAY STATS", true, kAccentDark);
}

void BarcodeApplication::drawDataPreview() {
    const Rect box = homeDataBoxRect(tft_.width(), tft_.height());
    tft_.fillRect(box.x + 4, box.y + 4, box.w - 8, box.h - 8, TFT_WHITE);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(TFT_BLACK, TFT_WHITE);
    const std::string text = printablePreview(spec_.data);
    const std::size_t charsPerLine = static_cast<std::size_t>(std::max(8, (box.w - 14) / 9));
    const std::size_t lines = static_cast<std::size_t>(std::max(1, (box.h - 11) / 19));
    std::size_t start = 0;
    if (text.size() > charsPerLine * lines) start = text.size() - charsPerLine * lines;
    for (std::size_t line = 0; line < lines; ++line) {
        const std::size_t pos = start + line * charsPerLine;
        if (pos >= text.size()) break;
        tft_.drawString(text.substr(pos, charsPerLine).c_str(),
                        box.x + 7,
                        box.y + 7 + static_cast<int>(line) * 19,
                        2);
    }
    tft_.setTextDatum(BR_DATUM);
    char count[24];
    std::snprintf(count, sizeof(count), "%u bytes", static_cast<unsigned>(spec_.data.size()));
    tft_.setTextColor(0x7BEF, TFT_WHITE);
    tft_.drawString(count, box.x + box.w - 5, box.y + box.h - 4, 1);
}

void BarcodeApplication::drawStatus() {
    const Rect rect = homeStatusRect(tft_.width(), tft_.height());
    tft_.fillRect(rect.x, rect.y, rect.w, rect.h, kBackground);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(kMuted, kBackground);
    const std::size_t maxChars = static_cast<std::size_t>(std::max(16, rect.w / 6));
    tft_.drawString(clipped(status_, maxChars).c_str(), rect.x + 1, rect.y, 1);
}

void BarcodeApplication::setStatus(const std::string& status, bool redraw) {
    status_ = status;
    if (redraw && view_ == View::Home) drawStatus();
}

void BarcodeApplication::drawKeyboard() {
    const uint16_t width = tft_.width();
    const KeyboardMetrics metrics = keyboardMetrics(width, tft_.height());

    const std::array<std::string, 4> upper = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-."};
    const std::array<std::string, 4> lower = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-."};
    const std::array<std::string, 4> numeric = {"1234567890", "0987654321", "-+*/=_.:,", "()[]{}<>"};
    const std::array<std::string, 4> symbols = {"@$%&#?!\\|", "-+*/=_.:,", "()[]{}<>", "'\";~^`"};
    const auto& rows = keyboardPage_ == KeyboardPage::Upper ? upper
                       : keyboardPage_ == KeyboardPage::Lower ? lower
                       : keyboardPage_ == KeyboardPage::Numeric ? numeric
                       : symbols;

    for (int row = 0; row < 4; ++row) {
        const std::string& keys = rows[static_cast<std::size_t>(row)];
        const int keyWidth = static_cast<int>(width) / static_cast<int>(keys.size());
        for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
            const int x = i * keyWidth;
            Rect rect{static_cast<int16_t>(x + 1),
                      static_cast<int16_t>(metrics.top + row * metrics.rowHeight + 1),
                      static_cast<int16_t>((i + 1 == static_cast<int>(keys.size())
                                                 ? static_cast<int>(width) - x
                                                 : keyWidth) - 2),
                      static_cast<int16_t>(metrics.rowHeight - 2)};
            drawButton(rect, std::string(1, keys[static_cast<std::size_t>(i)]), false, kPanelAlt);
        }
    }

    const int16_t controlY = static_cast<int16_t>(metrics.top + 4 * metrics.rowHeight);
    const int16_t controlH = static_cast<int16_t>(metrics.rowHeight - 1);
    const ControlRowLayout control = controlRowLayout(width, static_cast<int16_t>(controlY + 1), controlH);
    drawButton(control.keys[0],
               keyboardPage_ == KeyboardPage::Symbols
                   ? "FNC1"
                   : (keyboardPage_ == KeyboardPage::Upper ? "abc" : "ABC"));
    drawButton(control.keys[1], "SYM");
    drawButton(control.keys[2], "SPACE");
    drawButton(control.keys[3], "BKSP");
    drawButton(control.keys[4], "GO", true, kAccentDark);
}

void BarcodeApplication::handleHomeTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();

    if (gatewayModeActive_) {
        if (homeGatewayButtonRect(width, height).contains(x, y, kTouchPad)) {
            view_ = View::Gateway;
            gatewayStatsRedrawAt_ = 0;  // force an immediate draw rather than waiting for the next throttled refresh
            drawGateway();
        }
        return;
    }

    const auto topRow = homeTopRow(width, height);
    const auto actionRow = homeActionRow(width, height);
    const KeyboardMetrics metrics = keyboardMetrics(width, height);

    if (topRow[0].contains(x, y, kTouchPad)) {
        view_ = View::TypePicker;
        drawTypePicker();
    } else if (topRow[1].contains(x, y, kTouchPad)) {
        spec_.data.clear();
        setStatus("Payload cleared", false);
        drawDataPreview();
        drawStatus();
    } else if (topRow[2].contains(x, y, kTouchPad)) {
        const std::string slot = presets_.nextSlotName();
        std::string error;
        if (slot.empty()) setStatus("All 32 preset slots are occupied");
        else if (presets_.save(slot, spec_, error)) setStatus("Saved as " + slot);
        else setStatus("Save failed: " + error);
    } else if (actionRow[0].contains(x, y, kTouchPad)) {
        view_ = View::Options;
        drawOptions();
    } else if (actionRow[1].contains(x, y, kTouchPad)) {
        view_ = View::Presets;
        presetPage_ = 0;
        presetDeleteMode_ = false;
        drawPresets();
    } else if (actionRow[2].contains(x, y, kTouchPad)) {
        std::string error;
        if (!generate(spec_, true, error)) setStatus("Error: " + error);
    } else if (actionRow[3].contains(x, y, kTouchPad)) {
        spec_.data = randomPayloadFor(spec_.type);
        std::string error;
        if (!generate(spec_, true, error)) setStatus("Error: " + error);
    } else if (actionRow[4].contains(x, y, kTouchPad)) {
        view_ = View::Settings;
        drawSettings();
    } else if (y >= static_cast<uint16_t>(metrics.top)) {
        handleKeyboardTouch(x, y);
    }
}

void BarcodeApplication::handleKeyboardTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const KeyboardMetrics metrics = keyboardMetrics(width, tft_.height());

    const std::array<std::string, 4> upper = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-."};
    const std::array<std::string, 4> lower = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-."};
    const std::array<std::string, 4> numeric = {"1234567890", "0987654321", "-+*/=_.:,", "()[]{}<>"};
    const std::array<std::string, 4> symbols = {"@$%&#?!\\|", "-+*/=_.:,", "()[]{}<>", "'\";~^`"};
    const auto& rows = keyboardPage_ == KeyboardPage::Upper ? upper
                       : keyboardPage_ == KeyboardPage::Lower ? lower
                       : keyboardPage_ == KeyboardPage::Numeric ? numeric
                       : symbols;
    const int row = (static_cast<int>(y) - metrics.top) / metrics.rowHeight;
    if (row >= 0 && row < 4) {
        const std::string& keys = rows[static_cast<std::size_t>(row)];
        const int index = std::min<int>(static_cast<int>(keys.size()) - 1,
                                        static_cast<int>(x) * static_cast<int>(keys.size()) /
                                            static_cast<int>(width));
        appendCharacter(keys[static_cast<std::size_t>(index)]);
        return;
    }

    const ControlRowLayout control = controlRowLayout(width, 0, 1);
    if (x < static_cast<uint16_t>(control.keys[0].x + control.keys[0].w)) {
        if (keyboardPage_ == KeyboardPage::Symbols) appendText("{FNC1}");
        else {
            keyboardPage_ = keyboardPage_ == KeyboardPage::Upper ? KeyboardPage::Lower : KeyboardPage::Upper;
            drawKeyboard();
        }
    } else if (x < static_cast<uint16_t>(control.keys[1].x + control.keys[1].w)) {
        keyboardPage_ = keyboardPage_ == KeyboardPage::Symbols ? KeyboardPage::Numeric : KeyboardPage::Symbols;
        drawKeyboard();
    } else if (x < static_cast<uint16_t>(control.keys[2].x + control.keys[2].w)) {
        appendCharacter(' ');
    } else if (x < static_cast<uint16_t>(control.keys[3].x + control.keys[3].w)) {
        backspace();
    } else {
        std::string error;
        if (!generate(spec_, true, error)) setStatus("Error: " + error);
    }
}

void BarcodeApplication::appendCharacter(char c) {
    if (spec_.data.size() >= app_config::kMaxPayloadBytes) {
        setStatus("Payload limit reached");
        return;
    }
    spec_.data.push_back(c);
    drawDataPreview();
}

void BarcodeApplication::appendText(const std::string& text) {
    if (spec_.data.size() + text.size() > app_config::kMaxPayloadBytes) {
        setStatus("Payload limit reached");
        return;
    }
    spec_.data += text;
    drawDataPreview();
}

void BarcodeApplication::backspace() {
    if (!spec_.data.empty()) spec_.data.pop_back();
    drawDataPreview();
}

void BarcodeApplication::drawTypePicker() {
    applyOrientationForView(View::TypePicker);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;

    tft_.fillScreen(kBackground);
    const int16_t headerH = static_cast<int16_t>(wide ? 24 : 32);
    tft_.fillRect(0, 0, width, headerH, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("SELECT SYMBOLOGY", width / 2, headerH / 2, 4);

    const auto& types = supportedTypes();
    const int columns = wide ? 3 : 2;
    const int16_t cellW = static_cast<int16_t>(wide ? 149 : 148);
    const int16_t colStride = static_cast<int16_t>(wide ? 157 : 156);
    const int16_t rowStride = static_cast<int16_t>(wide ? 48 : 52);
    const int16_t cellH = 44;
    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 42);
    for (std::size_t i = 0; i < types.size(); ++i) {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        Rect rect{static_cast<int16_t>(8 + column * colStride),
                  static_cast<int16_t>(gridTop + row * rowStride),
                  cellW,
                  cellH};
        drawButton(rect, displayName(types[i]), types[i] == spec_.type);
    }

    const Rect back = wide ? Rect{8, static_cast<int16_t>(height - 48), static_cast<int16_t>(width - 16), 40}
                            : Rect{8, 430, 304, 42};
    drawButton(back, "BACK");
}

void BarcodeApplication::handleTypeTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;
    const int16_t backY = static_cast<int16_t>(wide ? height - 48 : 430);
    if (y >= static_cast<uint16_t>(backY - 5)) {
        showHome();
        return;
    }
    const int columns = wide ? 3 : 2;
    const int16_t colStride = static_cast<int16_t>(wide ? 157 : 156);
    const int16_t rowStride = static_cast<int16_t>(wide ? 48 : 52);
    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 42);
    if (y < static_cast<uint16_t>(gridTop)) return;
    const int column = std::min(columns - 1, static_cast<int>(x) / colStride);
    const int row = (static_cast<int>(y) - gridTop) / rowStride;
    const std::size_t index = static_cast<std::size_t>(row * columns + column);
    if (index < supportedTypes().size()) selectType(index);
}

void BarcodeApplication::selectType(std::size_t index) {
    spec_.type = supportedTypes()[index];
    showHome("Selected " + displayName(spec_.type));
}

void BarcodeApplication::drawOptions() {
    applyOrientationForView(View::Options);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;

    tft_.fillScreen(kBackground);
    const int16_t headerH = static_cast<int16_t>(wide ? 24 : 32);
    tft_.fillRect(0, 0, width, headerH, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("OPTIONS", width / 2, headerH / 2, 4);

    std::array<std::pair<std::string, std::string>, 9> rows = {{
        {"ECC", toString(spec_.ecc)},
        {"Rotation", toString(spec_.rotation)},
        {"Quiet zone", spec_.quietZone < 0 ? "default" : std::to_string(spec_.quietZone)},
        {"Min module", std::to_string(spec_.minModulePixels) + " px"},
        {"DM shape", spec_.dataMatrixRectangular ? "rectangle" : "square"},
        {"Invert", spec_.invert ? "yes" : "no"},
        {"Checksum", spec_.checksum ? "yes" : "no"},
        {"Aztec security", std::to_string(spec_.aztecSecurityPercent) + "%"},
        {"Aztec layers", std::to_string(spec_.aztecMinLayers)}
    }};

    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 38);
    const int16_t rowStride = static_cast<int16_t>(wide ? 26 : 41);
    const int16_t rowH = static_cast<int16_t>(wide ? 22 : 36);
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const int16_t y = static_cast<int16_t>(gridTop + row * rowStride);
        tft_.fillRoundRect(8, y, width - 16, rowH, 4, kPanel);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(kMuted, kPanel);
        tft_.drawString(rows[static_cast<std::size_t>(row)].first.c_str(), 16, y + rowH / 2, 2);
        tft_.setTextDatum(MR_DATUM);
        tft_.setTextColor(kText, kPanel);
        tft_.drawString(("<  " + rows[static_cast<std::size_t>(row)].second + "  >").c_str(),
                        width - 16, y + rowH / 2, 2);
    }
    if (wide) {
        drawButton({8, static_cast<int16_t>(height - 48), 228, 40}, "BACK");
        drawButton({244, static_cast<int16_t>(height - 48), 228, 40}, "DISPLAY", true, kAccentDark);
    } else {
        drawButton({8, 420, 145, 50}, "BACK");
        drawButton({167, 420, 145, 50}, "DISPLAY", true, kAccentDark);
    }
}

void BarcodeApplication::handleOptionsTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;
    const int16_t footerY = static_cast<int16_t>(wide ? height - 48 : 420);
    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 38);
    const int16_t rowStride = static_cast<int16_t>(wide ? 26 : 41);

    if (y >= static_cast<uint16_t>(footerY - 5)) {
        if (x < width / 2) showHome("Options updated");
        else {
            std::string error;
            if (!generate(spec_, true, error)) {
                showHome("Error: " + error);
            }
        }
        return;
    }
    if (y < static_cast<uint16_t>(gridTop)) return;
    const int row = (static_cast<int>(y) - gridTop) / rowStride;
    if (row >= 0 && row < 9) {
        cycleOption(row, x < width / 2 ? -1 : 1);
        drawOptions();
    }
}

void BarcodeApplication::cycleOption(int row, int direction) {
    switch (row) {
        case 0: {
            int value = static_cast<int>(spec_.ecc);
            value = (value + direction + 4) % 4;
            spec_.ecc = static_cast<ErrorCorrection>(value);
            break;
        }
        case 1: {
            static constexpr Rotation values[] = {Rotation::Auto, Rotation::Deg0, Rotation::Deg90,
                                                   Rotation::Deg180, Rotation::Deg270};
            int index = 0;
            for (int i = 0; i < 5; ++i) if (values[i] == spec_.rotation) index = i;
            index = (index + direction + 5) % 5;
            spec_.rotation = values[index];
            break;
        }
        case 2:
            spec_.quietZone += direction;
            if (spec_.quietZone < -1) spec_.quietZone = 20;
            if (spec_.quietZone > 20) spec_.quietZone = -1;
            break;
        case 3: {
            int value = std::clamp<int>(static_cast<int>(spec_.minModulePixels) + direction, 1, 8);
            spec_.minModulePixels = static_cast<uint8_t>(value);
            break;
        }
        case 4: spec_.dataMatrixRectangular = !spec_.dataMatrixRectangular; break;
        case 5: spec_.invert = !spec_.invert; break;
        case 6: spec_.checksum = !spec_.checksum; break;
        case 7: {
            int value = static_cast<int>(spec_.aztecSecurityPercent) + direction * 5;
            if (value < 5) value = 90;
            if (value > 90) value = 5;
            spec_.aztecSecurityPercent = static_cast<uint8_t>(value);
            break;
        }
        case 8: {
            int value = static_cast<int>(spec_.aztecMinLayers) + direction;
            if (value < 0) value = 32;
            if (value > 32) value = 0;
            spec_.aztecMinLayers = static_cast<uint8_t>(value);
            break;
        }
    }
}

void BarcodeApplication::drawPresets() {
    applyOrientationForView(View::Presets);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;

    tft_.fillScreen(kBackground);
    const int16_t headerH = static_cast<int16_t>(wide ? 24 : 32);
    tft_.fillRect(0, 0, width, headerH, presetDeleteMode_ ? kDanger : kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, presetDeleteMode_ ? kDanger : kAccentDark);
    tft_.drawString(presetDeleteMode_ ? "TAP PRESET TO DELETE" : "PRESETS", width / 2, headerH / 2, 4);

    const auto names = presets_.list();
    constexpr std::size_t pageSize = 8;
    const std::size_t pages = std::max<std::size_t>(1, (names.size() + pageSize - 1) / pageSize);
    if (presetPage_ >= pages) presetPage_ = pages - 1;
    const std::size_t start = presetPage_ * pageSize;

    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 42);
    const int16_t rowStride = static_cast<int16_t>(wide ? 29 : 45);
    const int16_t rowH = static_cast<int16_t>(wide ? 25 : 38);
    for (std::size_t i = 0; i < pageSize; ++i) {
        const std::size_t index = start + i;
        Rect rect{8, static_cast<int16_t>(gridTop + static_cast<int>(i) * rowStride),
                  static_cast<int16_t>(width - 16), rowH};
        if (index < names.size()) drawButton(rect, names[index], false, presetDeleteMode_ ? kDanger : kPanel);
        else drawButton(rect, "(empty)", false, kBackground);
    }

    if (wide) {
        const auto footer = distributeRow(width, static_cast<int16_t>(height - 48), 40, 4);
        drawButton(footer[0], "BACK");
        drawButton(footer[1], "SAVE");
        drawButton(footer[2], presetDeleteMode_ ? "CANCEL" : "DELETE", false, presetDeleteMode_ ? kDanger : kPanel);
        drawButton(footer[3], "NEXT");
    } else {
        drawButton({8, 410, 70, 60}, "BACK");
        drawButton({84, 410, 70, 60}, "SAVE");
        drawButton({160, 410, 70, 60}, presetDeleteMode_ ? "CANCEL" : "DELETE", false, presetDeleteMode_ ? kDanger : kPanel);
        drawButton({236, 410, 76, 60}, "NEXT");
    }
}

void BarcodeApplication::handlePresetsTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;
    const int16_t footerY = static_cast<int16_t>(wide ? height - 48 : 410);
    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 42);
    const int16_t rowStride = static_cast<int16_t>(wide ? 29 : 45);

    if (y >= static_cast<uint16_t>(footerY - 5)) {
        const std::vector<Rect> footer = wide
            ? distributeRow(width, footerY, 40, 4)
            : std::vector<Rect>{Rect{8, 410, 70, 60}, Rect{84, 410, 70, 60}, Rect{160, 410, 70, 60},
                                Rect{236, 410, 76, 60}};
        int index = 3;
        for (int i = 0; i < 4; ++i) {
            if (x < static_cast<uint16_t>(footer[static_cast<std::size_t>(i)].x +
                                          footer[static_cast<std::size_t>(i)].w)) {
                index = i;
                break;
            }
        }
        if (index == 0) {
            showHome();
            return;
        }
        if (index == 1) {
            const std::string slot = presets_.nextSlotName();
            std::string error;
            if (slot.empty()) {
                status_ = "Preset slots full";
            } else if (presets_.save(slot, spec_, error)) {
                status_ = "Saved as " + slot;
            } else {
                status_ = "Save failed: " + error;
            }
            drawPresets();
        } else if (index == 2) {
            presetDeleteMode_ = !presetDeleteMode_;
            drawPresets();
        } else {
            const auto names = presets_.list();
            const std::size_t pages = std::max<std::size_t>(1, (names.size() + 7) / 8);
            presetPage_ = (presetPage_ + 1) % pages;
            drawPresets();
        }
        return;
    }
    if (y < static_cast<uint16_t>(gridTop)) return;
    const int row = (static_cast<int>(y) - gridTop) / rowStride;
    if (row < 0 || row >= 8) return;
    const auto names = presets_.list();
    const std::size_t index = presetPage_ * 8 + static_cast<std::size_t>(row);
    if (index >= names.size()) return;

    std::string error;
    if (presetDeleteMode_) {
        if (presets_.remove(names[index], error)) status_ = "Deleted " + names[index];
        else status_ = "Delete failed: " + error;
        presetDeleteMode_ = false;
        drawPresets();
    } else {
        BarcodeSpec loaded;
        if (presets_.load(names[index], loaded, error)) {
            spec_ = std::move(loaded);
            showHome("Loaded " + names[index]);
        } else {
            status_ = "Load failed: " + error;
            drawPresets();
        }
    }
}

void BarcodeApplication::drawSettings() {
    applyOrientationForView(View::Settings);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;

    tft_.fillScreen(kBackground);
    const int16_t headerH = static_cast<int16_t>(wide ? 24 : 32);
    tft_.fillRect(0, 0, width, headerH, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("SETTINGS", width / 2, headerH / 2, 4);

    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 38);
    const int16_t rowStride = static_cast<int16_t>(wide ? 44 : 50);
    const int16_t rowH = static_cast<int16_t>(wide ? 38 : 44);
    const std::array<std::pair<const char*, ScreenOrientation>, 2> rows = {{
        {"Barcode orientation", config_.barcodeOrientation()},
        {"Editor orientation", config_.editorOrientation()},
    }};
    for (int row = 0; row < 2; ++row) {
        const int16_t y = static_cast<int16_t>(gridTop + row * rowStride);
        tft_.fillRoundRect(8, y, width - 16, rowH, 4, kPanel);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(kMuted, kPanel);
        tft_.drawString(rows[static_cast<std::size_t>(row)].first, 16, y + rowH / 2, 2);
        tft_.setTextDatum(MR_DATUM);
        tft_.setTextColor(kText, kPanel);
        const std::string label =
            std::string("<  ") + orientationLabel(rows[static_cast<std::size_t>(row)].second) + " deg  >";
        tft_.drawString(label.c_str(), width - 16, y + rowH / 2, 2);
    }

    const int16_t backY = static_cast<int16_t>(wide ? height - 48 : gridTop + 2 * rowStride + 10);
    drawButton({8, backY, static_cast<int16_t>(width - 16), 40}, "BACK");
}

void BarcodeApplication::handleSettingsTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;
    const int16_t gridTop = static_cast<int16_t>(wide ? 30 : 38);
    const int16_t rowStride = static_cast<int16_t>(wide ? 44 : 50);
    const int16_t backY = static_cast<int16_t>(wide ? height - 48 : gridTop + 2 * rowStride + 10);

    if (y >= static_cast<uint16_t>(backY - 5)) {
        showHome();
        return;
    }
    if (y < static_cast<uint16_t>(gridTop)) return;
    const int row = (static_cast<int>(y) - gridTop) / rowStride;
    if (row < 0 || row > 1) return;

    const int direction = x < width / 2 ? -1 : 1;
    const OrientationTarget target = row == 0 ? OrientationTarget::Barcode : OrientationTarget::Editor;
    const ScreenOrientation current = row == 0 ? config_.barcodeOrientation() : config_.editorOrientation();
    const int next = (static_cast<int>(current) + direction + 4) % 4;
    setOrientation(target, static_cast<ScreenOrientation>(next));
}

namespace {
constexpr int16_t kGatewayBackButtonHeight = 40;

std::string formatMac(const std::array<uint8_t, 6>& mac) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

std::string formatAgeSeconds(uint32_t nowMs, uint32_t lastSeenMs) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%lus ago", static_cast<unsigned long>((nowMs - lastSeenMs) / 1000));
    return buf;
}
}  // namespace

void BarcodeApplication::drawGateway() {
    applyOrientationForView(View::Gateway);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const bool wide = width > height;

    tft_.fillScreen(kBackground);
    const int16_t headerH = static_cast<int16_t>(wide ? 24 : 32);
    tft_.fillRect(0, 0, width, headerH, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("GATEWAY", width / 2, headerH / 2, 4);

    const esplink::GatewayStats::Snapshot& link = gatewayStats_.linkStats;
    const int16_t rowH = static_cast<int16_t>(wide ? 20 : 24);
    int16_t y = static_cast<int16_t>(headerH + 6);

    auto drawStatLine = [&](const std::string& label, const std::string& value, uint16_t valueColor) {
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(kMuted, kBackground);
        tft_.drawString(label.c_str(), 10, y + 3, 2);
        tft_.setTextDatum(TR_DATUM);
        tft_.setTextColor(valueColor, kBackground);
        tft_.drawString(value.c_str(), width - 10, y + 3, 2);
        y = static_cast<int16_t>(y + rowH);
    };

    const std::string hostValue = !link.hostEverSeen ? std::string("NONE")
                                   : link.hostConnected
                                       ? "UP (" + formatAgeSeconds(link.nowMs, link.hostLastSeenMs) + ")"
                                       : "LOST (" + formatAgeSeconds(link.nowMs, link.hostLastSeenMs) + ")";
    drawStatLine("USB host", hostValue, link.hostConnected ? kAccent : kDanger);

    char countBuf[16];
    std::snprintf(countBuf, sizeof(countBuf), "%u", static_cast<unsigned>(link.peerCount));
    drawStatLine("ESP-NOW clients", countBuf, kText);

    std::snprintf(countBuf, sizeof(countBuf), "%lu", static_cast<unsigned long>(gatewayStats_.usbToEspNowMessageCount));
    drawStatLine("Sent USB->ESPNOW", countBuf, kText);

    std::snprintf(countBuf, sizeof(countBuf), "%lu", static_cast<unsigned long>(gatewayStats_.espNowToUsbMessageCount));
    drawStatLine("Sent ESPNOW->USB", countBuf, kText);

    y = static_cast<int16_t>(y + 4);
    tft_.drawFastHLine(8, y, static_cast<int16_t>(width - 16), kPanelAlt);
    y = static_cast<int16_t>(y + 8);

    const int16_t backY = static_cast<int16_t>(height - kGatewayBackButtonHeight - 6);
    const int16_t peerRowH = static_cast<int16_t>(wide ? 16 : 18);
    std::size_t shown = 0;
    for (; shown < link.peerCount; ++shown) {
        if (y + peerRowH > backY - 4) break;
        const auto& peer = link.peers[shown];
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(kText, kBackground);
        tft_.drawString(formatMac(peer.mac).c_str(), 10, y + 2, 1);
        tft_.setTextDatum(TR_DATUM);
        tft_.setTextColor(kMuted, kBackground);
        tft_.drawString(formatAgeSeconds(link.nowMs, peer.lastSeenMs).c_str(), width - 10, y + 2, 1);
        y = static_cast<int16_t>(y + peerRowH);
    }
    if (shown < link.peerCount) {
        char more[24];
        std::snprintf(more, sizeof(more), "+%u more not shown", static_cast<unsigned>(link.peerCount - shown));
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(kMuted, kBackground);
        tft_.drawString(more, 10, y + 2, 1);
    }

    drawButton({8, backY, static_cast<int16_t>(width - 16), kGatewayBackButtonHeight}, "BACK");
}

void BarcodeApplication::handleGatewayTouch(uint16_t x, uint16_t y) {
    (void)x;
    const uint16_t height = tft_.height();
    const int16_t backY = static_cast<int16_t>(height - kGatewayBackButtonHeight - 6);
    if (y >= static_cast<uint16_t>(backY - kTouchPad)) {
        showHome();
    }
}

bool BarcodeApplication::generate(const BarcodeSpec& spec, bool display, std::string& error) {
    if (spec.data.size() > app_config::kMaxPayloadBytes) {
        error = "payload exceeds device limit";
        return false;
    }
    BarcodeResult result = encode(spec);
    if (!result.ok) {
        error = result.error;
        return false;
    }

    spec_ = spec;
    current_ = std::move(result);
    hasCurrent_ = true;
    currentIsRaw_ = false;
    currentQuietZone_ = static_cast<uint8_t>(std::clamp<int>(
        spec.quietZone < 0 ? current_.defaultQuietZone : spec.quietZone, 0, 32));
    currentRotation_ = spec.rotation;
    currentInvert_ = spec.invert;
    currentLabel_ = displayName(spec.type);
    status_ = "Generated " + currentLabel_;

    if (display && !displayCurrent(error)) return false;
    if (!display && view_ == View::Home) drawHome();
    return true;
}

bool BarcodeApplication::setUploadedMatrix(BitMatrix&& matrix,
                                           bool linear,
                                           uint8_t quietZone,
                                           Rotation rotation,
                                           bool invert,
                                           const std::string& label,
                                           bool display,
                                           std::string& error) {
    if (matrix.empty()) {
        error = "uploaded matrix is empty";
        return false;
    }

    BarcodeResult result;
    result.ok = true;
    result.matrix = std::move(matrix);
    result.linear = linear;
    result.defaultQuietZone = quietZone;
    result.normalizedData = label;
    current_ = std::move(result);
    hasCurrent_ = true;
    currentIsRaw_ = true;
    currentQuietZone_ = std::min<uint8_t>(quietZone, 32);
    currentRotation_ = rotation;
    currentInvert_ = invert;
    currentLabel_ = label.empty() ? "Uploaded matrix" : label;
    status_ = "Uploaded " + currentLabel_;
    if (display && !displayCurrent(error)) return false;
    return true;
}

bool BarcodeApplication::displayCurrent(std::string& error) {
    if (!hasCurrent_) {
        error = "no current symbol";
        return false;
    }
    if (!renderCurrent(error)) return false;
    view_ = View::Barcode;
    barcodeShownAt_ = millis();
    return true;
}

bool BarcodeApplication::renderCurrent(std::string& error) {
    applyOrientationForView(View::Barcode);
    const uint8_t minimum = currentIsRaw_ ? 1 : std::max<uint8_t>(spec_.minModulePixels, 1);
    const RenderLayout layout = calculateLayout(current_.matrix,
                                                current_.linear,
                                                tft_.width(),
                                                tft_.height(),
                                                currentRotation_,
                                                currentQuietZone_,
                                                minimum);
    if (!layout.ok) {
        error = layout.error;
        return false;
    }

    const uint16_t background = currentInvert_ ? TFT_BLACK : TFT_WHITE;
    const uint16_t foreground = currentInvert_ ? TFT_WHITE : TFT_BLACK;
    tft_.fillScreen(background);
    tft_.startWrite();
    const int scale = layout.modulePixels;
    const int quiet = layout.quietModules;
    const int width = current_.matrix.width();
    const int height = current_.linear ? 48 : current_.matrix.height();

    if (current_.linear) {
        for (int moduleX = 0; moduleX < width; ++moduleX) {
            if (!current_.matrix.get(static_cast<uint16_t>(moduleX), 0)) continue;
            switch (layout.rotation) {
                case Rotation::Deg0:
                    tft_.fillRect(layout.x + (quiet + moduleX) * scale,
                                  layout.y + quiet * scale,
                                  scale,
                                  height * scale,
                                  foreground);
                    break;
                case Rotation::Deg180:
                    tft_.fillRect(layout.x + (quiet + width - 1 - moduleX) * scale,
                                  layout.y + quiet * scale,
                                  scale,
                                  height * scale,
                                  foreground);
                    break;
                case Rotation::Deg90:
                    tft_.fillRect(layout.x + quiet * scale,
                                  layout.y + (quiet + moduleX) * scale,
                                  height * scale,
                                  scale,
                                  foreground);
                    break;
                case Rotation::Deg270:
                    tft_.fillRect(layout.x + quiet * scale,
                                  layout.y + (quiet + width - 1 - moduleX) * scale,
                                  height * scale,
                                  scale,
                                  foreground);
                    break;
                case Rotation::Auto: break;
            }
        }
    } else {
        for (int moduleY = 0; moduleY < height; ++moduleY) {
            for (int moduleX = 0; moduleX < width; ++moduleX) {
                if (!current_.matrix.get(static_cast<uint16_t>(moduleX),
                                         static_cast<uint16_t>(moduleY))) continue;
                int rx = moduleX;
                int ry = moduleY;
                switch (layout.rotation) {
                    case Rotation::Deg0: break;
                    case Rotation::Deg90: rx = height - 1 - moduleY; ry = moduleX; break;
                    case Rotation::Deg180: rx = width - 1 - moduleX; ry = height - 1 - moduleY; break;
                    case Rotation::Deg270: rx = moduleY; ry = width - 1 - moduleX; break;
                    case Rotation::Auto: break;
                }
                tft_.fillRect(layout.x + (quiet + rx) * scale,
                              layout.y + (quiet + ry) * scale,
                              scale,
                              scale,
                              foreground);
            }
        }
    }
    tft_.endWrite();
    return true;
}

void BarcodeApplication::closeBarcode() {
    if (view_ != View::Barcode) return;
    showHome("Closed barcode; current symbol retained");
}
