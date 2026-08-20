#include "BarcodeApplication.h"

#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <utility>

#include "RandomPayload.h"
#include "app_config.h"

using namespace espbarcode;

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
constexpr int kKeyboardTop = 238;
constexpr int kKeyboardRowHeight = 48;
// Extra hit-test tolerance for the home screen's small, tightly-packed
// buttons (e.g. CLEAR is only 70px wide with 8px gaps on either side).
constexpr int16_t kTouchPad = 4;

constexpr BarcodeApplication::Rect kTypeButton{8, 38, 150, 38};
constexpr BarcodeApplication::Rect kClearButton{166, 38, 70, 38};
constexpr BarcodeApplication::Rect kSaveButton{244, 38, 68, 38};
constexpr BarcodeApplication::Rect kDataBox{8, 82, 304, 88};
constexpr BarcodeApplication::Rect kOptionsButton{8, 176, 70, 38};
constexpr BarcodeApplication::Rect kPresetsButton{86, 176, 70, 38};
constexpr BarcodeApplication::Rect kDisplayButton{164, 176, 70, 38};
constexpr BarcodeApplication::Rect kRandomButton{242, 176, 70, 38};

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
    tft_.setRotation(0);
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

void BarcodeApplication::pollTouch() {
    const uint32_t now = millis();
    if (now - lastTouchPoll_ < 20) return;
    lastTouchPoll_ = now;

    uint16_t x = 0;
    uint16_t y = 0;
    const bool down = tft_.getTouch(&x, &y, app_config::kTouchThreshold);
    if (down && !touchDown_) handleTouch(x, y);
    touchDown_ = down;
}

void BarcodeApplication::handleTouch(uint16_t x, uint16_t y) {
    switch (view_) {
        case View::Home: handleHomeTouch(x, y); break;
        case View::TypePicker: handleTypeTouch(x, y); break;
        case View::Options: handleOptionsTouch(x, y); break;
        case View::Presets: handlePresetsTouch(x, y); break;
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

void BarcodeApplication::drawHome() {
    tft_.fillScreen(kBackground);
    tft_.fillRect(0, 0, app_config::kScreenWidth, 30, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("ESP BARCODE LAB", 160, 15, 4);

    drawButton(kTypeButton, displayName(spec_.type), true);
    drawButton(kClearButton, "CLEAR", false, kDanger);
    drawButton(kSaveButton, "SAVE", false, kPanelAlt);

    tft_.fillRoundRect(kDataBox.x, kDataBox.y, kDataBox.w, kDataBox.h, 5, TFT_WHITE);
    tft_.drawRoundRect(kDataBox.x, kDataBox.y, kDataBox.w, kDataBox.h, 5, kAccent);
    drawDataPreview();

    drawButton(kOptionsButton, "OPTIONS");
    drawButton(kPresetsButton, "PRESETS");
    drawButton(kDisplayButton, "DISPLAY", true, kAccentDark);
    drawButton(kRandomButton, "RANDOM", false, kWarning);
    drawStatus();
    drawKeyboard();
}

void BarcodeApplication::drawDataPreview() {
    tft_.fillRect(kDataBox.x + 4, kDataBox.y + 4, kDataBox.w - 8, kDataBox.h - 8, TFT_WHITE);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(TFT_BLACK, TFT_WHITE);
    const std::string text = printablePreview(spec_.data);
    constexpr std::size_t charsPerLine = 35;
    constexpr std::size_t lines = 4;
    std::size_t start = 0;
    if (text.size() > charsPerLine * lines) start = text.size() - charsPerLine * lines;
    for (std::size_t line = 0; line < lines; ++line) {
        const std::size_t pos = start + line * charsPerLine;
        if (pos >= text.size()) break;
        tft_.drawString(text.substr(pos, charsPerLine).c_str(),
                        kDataBox.x + 7,
                        kDataBox.y + 7 + static_cast<int>(line) * 19,
                        2);
    }
    tft_.setTextDatum(BR_DATUM);
    char count[24];
    std::snprintf(count, sizeof(count), "%u bytes", static_cast<unsigned>(spec_.data.size()));
    tft_.setTextColor(0x7BEF, TFT_WHITE);
    tft_.drawString(count, kDataBox.x + kDataBox.w - 5, kDataBox.y + kDataBox.h - 4, 1);
}

void BarcodeApplication::drawStatus() {
    tft_.fillRect(8, 217, 304, 17, kBackground);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(kMuted, kBackground);
    tft_.drawString(clipped(status_, 48).c_str(), 9, 217, 1);
}

void BarcodeApplication::setStatus(const std::string& status, bool redraw) {
    status_ = status;
    if (redraw && view_ == View::Home) drawStatus();
}

void BarcodeApplication::drawKeyboard() {
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
        const int keyWidth = app_config::kScreenWidth / static_cast<int>(keys.size());
        for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
            const int x = i * keyWidth;
            Rect rect{static_cast<int16_t>(x + 1),
                      static_cast<int16_t>(kKeyboardTop + row * kKeyboardRowHeight + 1),
                      static_cast<int16_t>((i + 1 == static_cast<int>(keys.size()) ? 320 - x : keyWidth) - 2),
                      static_cast<int16_t>(kKeyboardRowHeight - 2)};
            drawButton(rect, std::string(1, keys[static_cast<std::size_t>(i)]), false, kPanelAlt);
        }
    }

    const int y = kKeyboardTop + 4 * kKeyboardRowHeight;
    drawButton({1, static_cast<int16_t>(y + 1), 53, 47},
               keyboardPage_ == KeyboardPage::Symbols
                   ? "FNC1"
                   : (keyboardPage_ == KeyboardPage::Upper ? "abc" : "ABC"));
    drawButton({56, static_cast<int16_t>(y + 1), 53, 47}, "SYM");
    drawButton({111, static_cast<int16_t>(y + 1), 100, 47}, "SPACE");
    drawButton({213, static_cast<int16_t>(y + 1), 53, 47}, "BKSP");
    drawButton({268, static_cast<int16_t>(y + 1), 51, 47}, "GO", true, kAccentDark);
}

void BarcodeApplication::handleHomeTouch(uint16_t x, uint16_t y) {
    if (kTypeButton.contains(x, y, kTouchPad)) {
        view_ = View::TypePicker;
        drawTypePicker();
    } else if (kClearButton.contains(x, y, kTouchPad)) {
        spec_.data.clear();
        setStatus("Payload cleared", false);
        drawDataPreview();
        drawStatus();
    } else if (kSaveButton.contains(x, y, kTouchPad)) {
        const std::string slot = presets_.nextSlotName();
        std::string error;
        if (slot.empty()) setStatus("All 32 preset slots are occupied");
        else if (presets_.save(slot, spec_, error)) setStatus("Saved as " + slot);
        else setStatus("Save failed: " + error);
    } else if (kOptionsButton.contains(x, y, kTouchPad)) {
        view_ = View::Options;
        drawOptions();
    } else if (kPresetsButton.contains(x, y, kTouchPad)) {
        view_ = View::Presets;
        presetPage_ = 0;
        presetDeleteMode_ = false;
        drawPresets();
    } else if (kDisplayButton.contains(x, y, kTouchPad)) {
        std::string error;
        if (!generate(spec_, true, error)) setStatus("Error: " + error);
    } else if (kRandomButton.contains(x, y, kTouchPad)) {
        spec_.data = randomPayloadFor(spec_.type);
        std::string error;
        if (!generate(spec_, true, error)) setStatus("Error: " + error);
    } else if (y >= kKeyboardTop) {
        handleKeyboardTouch(x, y);
    }
}

void BarcodeApplication::handleKeyboardTouch(uint16_t x, uint16_t y) {
    const std::array<std::string, 4> upper = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-."};
    const std::array<std::string, 4> lower = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-."};
    const std::array<std::string, 4> numeric = {"1234567890", "0987654321", "-+*/=_.:,", "()[]{}<>"};
    const std::array<std::string, 4> symbols = {"@$%&#?!\\|", "-+*/=_.:,", "()[]{}<>", "'\";~^`"};
    const auto& rows = keyboardPage_ == KeyboardPage::Upper ? upper
                       : keyboardPage_ == KeyboardPage::Lower ? lower
                       : keyboardPage_ == KeyboardPage::Numeric ? numeric
                       : symbols;
    const int row = (static_cast<int>(y) - kKeyboardTop) / kKeyboardRowHeight;
    if (row >= 0 && row < 4) {
        const std::string& keys = rows[static_cast<std::size_t>(row)];
        const int index = std::min<int>(keys.size() - 1,
                                        static_cast<int>(x) * static_cast<int>(keys.size()) / 320);
        appendCharacter(keys[static_cast<std::size_t>(index)]);
        return;
    }

    if (x < 55) {
        if (keyboardPage_ == KeyboardPage::Symbols) appendText("{FNC1}");
        else {
            keyboardPage_ = keyboardPage_ == KeyboardPage::Upper ? KeyboardPage::Lower : KeyboardPage::Upper;
            drawKeyboard();
        }
    } else if (x < 110) {
        keyboardPage_ = keyboardPage_ == KeyboardPage::Symbols ? KeyboardPage::Numeric : KeyboardPage::Symbols;
        drawKeyboard();
    } else if (x < 212) {
        appendCharacter(' ');
    } else if (x < 267) {
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
    tft_.fillScreen(kBackground);
    tft_.fillRect(0, 0, 320, 32, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("SELECT SYMBOLOGY", 160, 16, 4);
    const auto& types = supportedTypes();
    for (std::size_t i = 0; i < types.size(); ++i) {
        const int column = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        Rect rect{static_cast<int16_t>(8 + column * 156),
                  static_cast<int16_t>(42 + row * 52),
                  148,
                  44};
        drawButton(rect, displayName(types[i]), types[i] == spec_.type);
    }
    drawButton({8, 430, 304, 42}, "BACK");
}

void BarcodeApplication::handleTypeTouch(uint16_t x, uint16_t y) {
    if (y >= 425) {
        showHome();
        return;
    }
    if (y < 42) return;
    const int column = x >= 160 ? 1 : 0;
    const int row = (static_cast<int>(y) - 42) / 52;
    const std::size_t index = static_cast<std::size_t>(row * 2 + column);
    if (index < supportedTypes().size()) selectType(index);
}

void BarcodeApplication::selectType(std::size_t index) {
    spec_.type = supportedTypes()[index];
    showHome("Selected " + displayName(spec_.type));
}

void BarcodeApplication::drawOptions() {
    tft_.fillScreen(kBackground);
    tft_.fillRect(0, 0, 320, 32, kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, kAccentDark);
    tft_.drawString("OPTIONS", 160, 16, 4);

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

    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const int y = 38 + row * 41;
        tft_.fillRoundRect(8, y, 304, 36, 4, kPanel);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(kMuted, kPanel);
        tft_.drawString(rows[static_cast<std::size_t>(row)].first.c_str(), 16, y + 18, 2);
        tft_.setTextDatum(MR_DATUM);
        tft_.setTextColor(kText, kPanel);
        tft_.drawString(("<  " + rows[static_cast<std::size_t>(row)].second + "  >").c_str(), 304, y + 18, 2);
    }
    drawButton({8, 420, 145, 50}, "BACK");
    drawButton({167, 420, 145, 50}, "DISPLAY", true, kAccentDark);
}

void BarcodeApplication::handleOptionsTouch(uint16_t x, uint16_t y) {
    if (y >= 415) {
        if (x < 160) showHome("Options updated");
        else {
            std::string error;
            if (!generate(spec_, true, error)) {
                showHome("Error: " + error);
            }
        }
        return;
    }
    if (y < 38) return;
    const int row = (static_cast<int>(y) - 38) / 41;
    if (row >= 0 && row < 9) {
        cycleOption(row, x < 160 ? -1 : 1);
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
    tft_.fillScreen(kBackground);
    tft_.fillRect(0, 0, 320, 32, presetDeleteMode_ ? kDanger : kAccentDark);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(kText, presetDeleteMode_ ? kDanger : kAccentDark);
    tft_.drawString(presetDeleteMode_ ? "TAP PRESET TO DELETE" : "PRESETS", 160, 16, 4);

    const auto names = presets_.list();
    constexpr std::size_t pageSize = 8;
    const std::size_t pages = std::max<std::size_t>(1, (names.size() + pageSize - 1) / pageSize);
    if (presetPage_ >= pages) presetPage_ = pages - 1;
    const std::size_t start = presetPage_ * pageSize;
    for (std::size_t i = 0; i < pageSize; ++i) {
        const std::size_t index = start + i;
        Rect rect{8, static_cast<int16_t>(42 + static_cast<int>(i) * 45), 304, 38};
        if (index < names.size()) drawButton(rect, names[index], false, presetDeleteMode_ ? kDanger : kPanel);
        else drawButton(rect, "(empty)", false, kBackground);
    }

    drawButton({8, 410, 70, 60}, "BACK");
    drawButton({84, 410, 70, 60}, "SAVE");
    drawButton({160, 410, 70, 60}, presetDeleteMode_ ? "CANCEL" : "DELETE", false, presetDeleteMode_ ? kDanger : kPanel);
    drawButton({236, 410, 76, 60}, "NEXT");
}

void BarcodeApplication::handlePresetsTouch(uint16_t x, uint16_t y) {
    if (y >= 405) {
        if (x < 80) showHome();
        else if (x < 158) {
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
        } else if (x < 233) {
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
    if (y < 42) return;
    const int row = (static_cast<int>(y) - 42) / 45;
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
    const uint8_t minimum = currentIsRaw_ ? 1 : std::max<uint8_t>(spec_.minModulePixels, 1);
    const RenderLayout layout = calculateLayout(current_.matrix,
                                                current_.linear,
                                                app_config::kScreenWidth,
                                                app_config::kScreenHeight,
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
