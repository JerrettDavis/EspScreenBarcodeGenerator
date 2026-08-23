#include "BarcodeApplication.h"

#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "RandomPayload.h"
#include "app_config.h"

using namespace espbarcode;
using esplink::OrientationTarget;
using esplink::ScreenOrientation;
using uigeom::Theme;

namespace {
// Extra hit-test tolerance for small, tightly-packed touch targets (icon
// buttons, switches, list rows) -- resistive-touch calibration always has a
// few pixels of slop.
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
// Icon vocabulary. Every icon is a small stroke-based glyph drawn with plain
// TFT_eSPI primitives (drawWideLine gives a clean ~1.6px anti-aliased stroke)
// centered on (cx, cy) within a ~14-16px box. `bg` is the color the icon sits
// on -- drawWideLine anti-aliases by blending against it, so it must match
// whatever was just filled at that spot.
// ---------------------------------------------------------------------------

void wl(TFT_eSPI& g, float x0, float y0, float x1, float y1, uint16_t color, uint16_t bg) {
    g.drawWideLine(x0, y0, x1, y1, 1.6f, color, bg);
}

void iconBarcode(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t /*bg*/) {
    const int16_t widths[5] = {1, 2, 1, 2, 1};
    int16_t x = static_cast<int16_t>(cx - 6);
    const int16_t y = static_cast<int16_t>(cy - 6);
    for (int16_t w : widths) {
        g.fillRect(x, y, w, 12, c);
        x = static_cast<int16_t>(x + w + 1);
    }
}

void iconChevronDown(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    wl(g, cx - 4, cy - 2, cx, cy + 2, c, bg);
    wl(g, cx, cy + 2, cx + 4, cy - 2, c, bg);
}

void iconChevronLeft(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    wl(g, cx + 2, cy - 4, cx - 2, cy, c, bg);
    wl(g, cx - 2, cy, cx + 2, cy + 4, c, bg);
}

void iconChevronRight(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    wl(g, cx - 2, cy - 4, cx + 2, cy, c, bg);
    wl(g, cx + 2, cy, cx - 2, cy + 4, c, bg);
}

void iconBookmark(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    const int16_t x0 = static_cast<int16_t>(cx - 5), x1 = static_cast<int16_t>(cx + 5);
    const int16_t y0 = static_cast<int16_t>(cy - 6), yb = static_cast<int16_t>(cy + 5);
    wl(g, x0, y0, x1, y0, c, bg);
    wl(g, x0, y0, x0, yb, c, bg);
    wl(g, x1, y0, x1, yb, c, bg);
    wl(g, x0, yb, cx, cy + 1, c, bg);
    wl(g, cx, cy + 1, x1, yb, c, bg);
}

void iconSliders(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    const int16_t x0 = static_cast<int16_t>(cx - 6), x1 = static_cast<int16_t>(cx + 6);
    const int16_t ys[3] = {static_cast<int16_t>(cy - 4), cy, static_cast<int16_t>(cy + 4)};
    const int16_t knob[3] = {static_cast<int16_t>(cx - 2), static_cast<int16_t>(cx + 3), static_cast<int16_t>(cx - 4)};
    for (int i = 0; i < 3; ++i) {
        wl(g, x0, ys[i], x1, ys[i], c, bg);
        g.fillCircle(knob[i], ys[i], 2, c);
    }
}

void iconList(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    const int16_t x0 = static_cast<int16_t>(cx - 6), x1 = static_cast<int16_t>(cx + 6);
    const int16_t ys[3] = {static_cast<int16_t>(cy - 4), cy, static_cast<int16_t>(cy + 4)};
    for (int i = 0; i < 3; ++i) wl(g, x0, ys[i], i == 2 ? static_cast<int16_t>(cx + 1) : x1, ys[i], c, bg);
}

void iconEye(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t /*bg*/) {
    g.drawEllipse(cx, cy, 7, 4, c);
    g.fillCircle(cx, cy, 2, c);
}

void iconShuffle(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    wl(g, cx - 6, cy - 4, cx + 3, cy + 4, c, bg);
    wl(g, cx - 6, cy + 4, cx + 3, cy - 4, c, bg);
    wl(g, cx + 3, cy + 4, cx + 6, cy + 4, c, bg);
    wl(g, cx + 3, cy - 4, cx + 6, cy - 4, c, bg);
}

void iconGear(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    g.drawCircle(cx, cy, 4, c);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 3.14159265f / 4.0f;
        const int16_t x0 = static_cast<int16_t>(static_cast<float>(cx) + cosf(a) * 5.0f);
        const int16_t y0 = static_cast<int16_t>(static_cast<float>(cy) + sinf(a) * 5.0f);
        const int16_t x1 = static_cast<int16_t>(static_cast<float>(cx) + cosf(a) * 7.0f);
        const int16_t y1 = static_cast<int16_t>(static_cast<float>(cy) + sinf(a) * 7.0f);
        wl(g, x0, y0, x1, y1, c, bg);
    }
}

void iconSun(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    g.drawCircle(cx, cy, 3, c);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * 3.14159265f / 4.0f;
        const int16_t x0 = static_cast<int16_t>(static_cast<float>(cx) + cosf(a) * 5.0f);
        const int16_t y0 = static_cast<int16_t>(static_cast<float>(cy) + sinf(a) * 5.0f);
        const int16_t x1 = static_cast<int16_t>(static_cast<float>(cx) + cosf(a) * 7.0f);
        const int16_t y1 = static_cast<int16_t>(static_cast<float>(cy) + sinf(a) * 7.0f);
        wl(g, x0, y0, x1, y1, c, bg);
    }
}

void iconMoon(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    g.fillCircle(cx, cy, 5, c);
    g.fillCircle(static_cast<int16_t>(cx + 3), static_cast<int16_t>(cy - 2), 5, bg);
}

void iconClose(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    wl(g, cx - 5, cy - 5, cx + 5, cy + 5, c, bg);
    wl(g, cx + 5, cy - 5, cx - 5, cy + 5, c, bg);
}

void iconBackspace(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    const int16_t x0 = static_cast<int16_t>(cx - 7), y0 = static_cast<int16_t>(cy - 5);
    const int16_t x1 = static_cast<int16_t>(cx + 7), y1 = static_cast<int16_t>(cy + 5);
    const int16_t px = static_cast<int16_t>(cx - 11);
    wl(g, px, cy, x0, y0, c, bg);
    wl(g, x0, y0, x1, y0, c, bg);
    wl(g, x1, y0, x1, y1, c, bg);
    wl(g, x1, y1, x0, y1, c, bg);
    wl(g, x0, y1, px, cy, c, bg);
    wl(g, cx - 2, cy - 3, cx + 3, cy + 3, c, bg);
    wl(g, cx + 3, cy - 3, cx - 2, cy + 3, c, bg);
}

void iconRestart(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t c, uint16_t bg) {
    g.drawCircle(cx, cy, 6, c);
    g.drawCircle(cx, cy, 5, c);
    g.fillTriangle(static_cast<int16_t>(cx + 4), static_cast<int16_t>(cy - 8),
                   static_cast<int16_t>(cx + 9), static_cast<int16_t>(cy - 5),
                   static_cast<int16_t>(cx + 3), static_cast<int16_t>(cy - 3), c);
    (void)bg;
}

// ---------------------------------------------------------------------------
// Editor screen layout. The editor (Home/TypePicker/Options/Presets/Settings/
// keyboard) is user-orientable independent of the barcode display, so every
// rect below is derived from the *current* screen dimensions rather than a
// fixed 320x480 assumption. `wide` (landscape, 480x320) is the primary target;
// portrait (320x480) gets a taller, more generously spaced variant of the
// same layout.
// ---------------------------------------------------------------------------

// Evenly distributes `count` cells across [margin, width-margin] at row y/h.
std::vector<Rect> distributeRow(uint16_t width, int16_t y, int16_t h, int count,
                                int16_t margin = 8, int16_t gap = 8) {
    std::vector<Rect> rects;
    rects.reserve(static_cast<std::size_t>(count));
    const int totalGap = gap * (count - 1);
    const int cellWidth = (static_cast<int>(width) - 2 * margin - totalGap) / count;
    int16_t x = margin;
    for (int i = 0; i < count; ++i) {
        rects.push_back(Rect{x, y, static_cast<int16_t>(cellWidth), h});
        x = static_cast<int16_t>(x + cellWidth + gap);
    }
    return rects;
}

// ---- Home top bar (symbology chip, byte count, save, theme toggle) ----

int16_t homeTopBarHeight(uint16_t width, uint16_t height) { return width > height ? 26 : 30; }

Rect homeToggleRect(uint16_t width, uint16_t height) {
    const int16_t barH = homeTopBarHeight(width, height);
    const int16_t sw = 42, sh = static_cast<int16_t>(barH - 6);
    return Rect{static_cast<int16_t>(width - sw - 6), static_cast<int16_t>((barH - sh) / 2), sw, sh};
}

Rect homeSaveIconRect(uint16_t width, uint16_t height) {
    const Rect toggle = homeToggleRect(width, height);
    return Rect{static_cast<int16_t>(toggle.x - toggle.h - 6), toggle.y, toggle.h, toggle.h};
}

Rect homeChipRect(uint16_t width, uint16_t height) {
    const int16_t barH = homeTopBarHeight(width, height);
    const int16_t h = static_cast<int16_t>(barH - 4);
    const int16_t w = static_cast<int16_t>(width > height ? 176 : 190);
    return Rect{6, 2, w, h};
}

// ---- Data preview card ----

Rect homeDataBoxRect(uint16_t width, uint16_t height) {
    const bool wide = width > height;
    const int16_t top = static_cast<int16_t>(homeTopBarHeight(width, height) + 6);
    return wide ? Rect{8, top, static_cast<int16_t>(width - 16), 50}
                : Rect{8, top, static_cast<int16_t>(width - 16), 78};
}

Rect homeClearButtonRect(uint16_t width, uint16_t height) {
    const Rect box = homeDataBoxRect(width, height);
    return Rect{static_cast<int16_t>(box.x + box.w - 28), static_cast<int16_t>(box.y + box.h / 2 - 10), 20, 20};
}

// ---- Flat action row (OPTIONS / PRESETS / DISPLAY / RANDOM / SETTINGS) ----

std::vector<Rect> homeActionRow(uint16_t width, uint16_t height) {
    const Rect box = homeDataBoxRect(width, height);
    const int16_t y = static_cast<int16_t>(box.y + box.h + 6);
    const int16_t h = static_cast<int16_t>(width > height ? 40 : 46);
    return distributeRow(width, y, h, 5, 8, 0);
}

Rect homeStatusRect(uint16_t width, uint16_t height) {
    const auto row = homeActionRow(width, height);
    const int16_t y = static_cast<int16_t>(row.front().y + row.front().h + 4);
    const int16_t h = static_cast<int16_t>(width > height ? 12 : 16);
    return Rect{8, y, static_cast<int16_t>(width - 16), h};
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

// ---- Shared subheader (back chevron + title + theme toggle) ----

int16_t subHeaderHeight(uint16_t width, uint16_t height) { return width > height ? 28 : 32; }

Rect subHeaderBackRect(uint16_t width, uint16_t height) {
    const int16_t h = static_cast<int16_t>(subHeaderHeight(width, height) - 4);
    return Rect{4, 2, h, h};
}

Rect subHeaderToggleRect(uint16_t width, uint16_t height) {
    const int16_t barH = subHeaderHeight(width, height);
    const int16_t sw = 42, sh = static_cast<int16_t>(barH - 8);
    return Rect{static_cast<int16_t>(width - sw - 6), static_cast<int16_t>((barH - sh) / 2), sw, sh};
}

// ---- Gateway-mode Home banner ----

int16_t gatewayBannerIconCy(uint16_t width, uint16_t height) {
    const int16_t contentTop = subHeaderHeight(width, height);
    return static_cast<int16_t>(contentTop + (height - contentTop) / 2 - 44);
}

std::array<Rect, 2> gatewayBannerButtons(uint16_t width, uint16_t height) {
    const int16_t y = static_cast<int16_t>(gatewayBannerIconCy(width, height) + 26 + 54);
    const int16_t gap = 10;
    const int16_t btnW = static_cast<int16_t>((static_cast<int>(width) - 16 - gap) / 2);
    return {Rect{8, y, btnW, 40}, Rect{static_cast<int16_t>(8 + btnW + gap), y, btnW, 40}};
}

// ---- TypePicker grid ----

struct TypeGrid {
    int columns;
    int16_t left;
    int16_t top;
    int16_t cellW;
    int16_t cellH;
    int16_t colStride;
    int16_t rowStride;
};

TypeGrid typeGridMetrics(uint16_t width, uint16_t height, int16_t contentTop, int count) {
    const bool wide = width > height;
    const int columns = wide ? 3 : 2;
    const int rows = (count + columns - 1) / columns;
    const int16_t gap = 6;
    const int16_t left = 8;
    const int16_t bottomMargin = 8;
    const int16_t availW = static_cast<int16_t>(width - 2 * left - (columns - 1) * gap);
    const int16_t cellW = static_cast<int16_t>(availW / columns);
    const int16_t availH = static_cast<int16_t>(height - contentTop - bottomMargin);
    const int16_t rowStride = static_cast<int16_t>(availH / rows);
    const int16_t cellH = static_cast<int16_t>(rowStride - gap);
    return TypeGrid{columns, left, contentTop, cellW, cellH,
                    static_cast<int16_t>(cellW + gap), rowStride};
}

// ---- Options grouped list ----

int16_t optionsFooterHeight(uint16_t width, uint16_t height) { return width > height ? 34 : 42; }

Rect optionsListCardRect(uint16_t width, uint16_t height, int16_t contentTop, int16_t footerH) {
    const int16_t y = static_cast<int16_t>(contentTop + 5);
    const int16_t h = static_cast<int16_t>(height - y - footerH - 10);
    return Rect{8, y, static_cast<int16_t>(width - 16), h};
}

Rect optionsDisplayButtonRect(uint16_t width, uint16_t height) {
    const int16_t h = optionsFooterHeight(width, height);
    return Rect{8, static_cast<int16_t>(height - h - 6), static_cast<int16_t>(width - 16), h};
}

// ---- Settings grouped list ----

Rect settingsCardRect(uint16_t width, uint16_t height, int16_t contentTop) {
    const int16_t y = static_cast<int16_t>(contentTop + 6);
    const int16_t h = static_cast<int16_t>(std::min<int>(120, height - contentTop - 14));
    return Rect{8, y, static_cast<int16_t>(width - 16), h};
}

// ---- Presets grouped list + footer ----

int16_t presetsFooterHeight(uint16_t width, uint16_t height) { return width > height ? 36 : 44; }

Rect presetsListCardRect(uint16_t width, uint16_t height, int16_t contentTop, int16_t footerH) {
    const int16_t y = static_cast<int16_t>(contentTop + 5);
    const int16_t h = static_cast<int16_t>(height - y - footerH - 10);
    return Rect{8, y, static_cast<int16_t>(width - 16), h};
}

std::array<Rect, 3> presetsFooterButtons(uint16_t width, uint16_t height) {
    const int16_t h = presetsFooterHeight(width, height);
    const int16_t y = static_cast<int16_t>(height - h - 6);
    const auto row = distributeRow(width, y, h, 3);
    return {row[0], row[1], row[2]};
}

// ---- Gateway stats ----

Rect gatewayStatusPillRect(int16_t contentTop) {
    return Rect{8, static_cast<int16_t>(contentTop + 6), 170, 22};
}

Rect gatewayPingButtonRect(int16_t contentTop, uint16_t width) {
    constexpr int16_t w = 68;
    return Rect{static_cast<int16_t>(width - w - 8), static_cast<int16_t>(contentTop + 6), w, 22};
}

Rect gatewayLinkStatusRowRect(uint16_t width, int16_t cardBottom) {
    return Rect{8, static_cast<int16_t>(cardBottom + 12), static_cast<int16_t>(width - 16), 28};
}

std::array<Rect, 3> gatewayStatTiles(uint16_t width, int16_t y) {
    const auto row = distributeRow(width, y, 40, 3, 8, 6);
    return {row[0], row[1], row[2]};
}

Rect gatewayRestartButtonRect(uint16_t width, uint16_t height) {
    return Rect{8, static_cast<int16_t>(height - 42), static_cast<int16_t>(width - 16), 34};
}

Rect gatewayPeersCardRect(uint16_t width, uint16_t height, int16_t statsY) {
    const int16_t y = static_cast<int16_t>(statsY + 46);
    const int16_t h = static_cast<int16_t>(height - y - 42 - 10);
    return Rect{8, y, static_cast<int16_t>(width - 16), h};
}

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

// ---- Settings -> Trust navigation button ----

// y-coordinate right below whatever Settings currently renders below the orientation/pairing
// card -- the ESP-NOW gateway-link status row when this board is a plain client, or just the
// card itself in gateway mode (see drawSettings()).
int16_t settingsContentBottomY(uint16_t width, const Rect& card, bool gatewayModeActive) {
    if (gatewayModeActive) return static_cast<int16_t>(card.y + card.h);
    const Rect linkRow = gatewayLinkStatusRowRect(width, static_cast<int16_t>(card.y + card.h));
    return static_cast<int16_t>(linkRow.y + linkRow.h);
}

Rect settingsTrustButtonRect(uint16_t width, const Rect& card, bool gatewayModeActive) {
    const int16_t y = static_cast<int16_t>(settingsContentBottomY(width, card, gatewayModeActive) + 10);
    return Rect{8, y, static_cast<int16_t>(width - 16), 36};
}

// ---- Trust screen ----

// This file duplicates small MAC-formatting helpers per class rather than sharing them --
// see formatMac's independent copies in EspNowEndpoint.cpp/GatewayRelay.cpp. macFromString is
// the inverse of those: parsing a "AA:BB:CC:DD:EE:FF"-formatted string (as stored in
// GatewayLinkInfo::gatewayId/GatewayStats::Peer::mac's formatted form) back into raw bytes.
bool macFromString(const std::string& text, std::array<uint8_t, 6>& out) {
    unsigned values[6];
    if (std::sscanf(text.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &values[0], &values[1], &values[2], &values[3],
                    &values[4], &values[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) out[static_cast<std::size_t>(i)] = static_cast<uint8_t>(values[i]);
    return true;
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

std::string BarcodeApplication::symbologyHint(Symbology type) {
    switch (type) {
        case Symbology::QrCode:
        case Symbology::DataMatrix:
        case Symbology::Aztec:
            return "2D";
        case Symbology::UpcA:
        case Symbology::Ean13:
        case Symbology::Ean8:
            return "RETAIL";
        default:
            return "LINEAR";
    }
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
    tft_.fillScreen(theme().bg);

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
    redrawView(view_);
}

void BarcodeApplication::toggleTheme() {
    std::string error;
    if (!config_.setDarkTheme(!config_.darkTheme(), error)) {
        setStatus("Theme save failed: " + error, view_ == View::Home);
        return;
    }
    redrawView(view_);
}

void BarcodeApplication::redrawView(View view) {
    switch (view) {
        case View::Home: drawHome(); break;
        case View::TypePicker: drawTypePicker(); break;
        case View::Options: drawOptions(); break;
        case View::Presets: drawPresets(); break;
        case View::Settings: drawSettings(); break;
        case View::Gateway: drawGateway(); break;
        case View::Trust: drawTrust(); break;
        case View::Barcode: break;  // always white/black regardless of theme; nothing to restyle
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
        case View::Trust: handleTrustTouch(x, y); break;
        case View::Barcode:
            if (millis() - barcodeShownAt_ >= app_config::kTouchCloseGuardMs) closeBarcode();
            break;
    }
}

void BarcodeApplication::drawButton(const Rect& rect,
                                    const std::string& text,
                                    bool selected,
                                    uint16_t fill) {
    const Theme& th = theme();
    if (fill == 0) fill = selected ? th.accent : th.surfaceAlt;
    uint16_t textColor = th.text;
    if (fill == th.accent) textColor = th.accentText;
    else if (fill == th.danger) textColor = TFT_WHITE;
    const uint16_t border = (fill == th.surfaceAlt || fill == th.surface) ? th.hairline : fill;
    tft_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 8, fill);
    tft_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, border);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(textColor, fill);
    tft_.drawString(clipped(text, 19).c_str(), rect.x + rect.w / 2, rect.y + rect.h / 2, 2);
}

void BarcodeApplication::drawThemeSwitch(const Rect& rect) {
    const Theme& th = theme();
    const bool dark = config_.darkTheme();
    tft_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, th.surfaceAlt);
    tft_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, th.hairline);
    iconSun(tft_, static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h / 2), th.textFaint, th.surfaceAlt);
    iconMoon(tft_, static_cast<int16_t>(rect.x + rect.w - 8), static_cast<int16_t>(rect.y + rect.h / 2), th.textFaint, th.surfaceAlt);
    const int16_t knobD = static_cast<int16_t>(rect.h - 4);
    const int16_t knobCx = static_cast<int16_t>(dark ? rect.x + rect.w - knobD / 2 - 2 : rect.x + knobD / 2 + 2);
    tft_.fillCircle(knobCx, static_cast<int16_t>(rect.y + rect.h / 2), knobD / 2, th.accent);
}

void BarcodeApplication::drawMiniSwitch(const Rect& rect, bool on) {
    const Theme& th = theme();
    const uint16_t track = on ? th.accent : th.surfaceAlt;
    tft_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, track);
    tft_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, rect.h / 2, on ? th.accent : th.hairline);
    const int16_t knobD = static_cast<int16_t>(rect.h - 4);
    const int16_t knobCx = static_cast<int16_t>(on ? rect.x + rect.w - knobD / 2 - 2 : rect.x + knobD / 2 + 2);
    tft_.fillCircle(knobCx, static_cast<int16_t>(rect.y + rect.h / 2), knobD / 2, on ? TFT_WHITE : th.textMuted);
}

int16_t BarcodeApplication::drawSubHeader(const std::string& title, bool showBack) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();
    const int16_t h = subHeaderHeight(width, height);
    tft_.fillRect(0, 0, width, h, th.bg);
    int16_t textX = 10;
    if (showBack) {
        const Rect back = subHeaderBackRect(width, height);
        iconChevronLeft(tft_, static_cast<int16_t>(back.x + back.w / 2), static_cast<int16_t>(back.y + back.h / 2),
                        th.textMuted, th.bg);
        textX = static_cast<int16_t>(back.x + back.w + 6);
    }
    tft_.setTextDatum(ML_DATUM);
    tft_.setTextColor(th.text, th.bg);
    tft_.drawString(title.c_str(), textX, static_cast<int16_t>(h / 2), 4);
    drawThemeSwitch(subHeaderToggleRect(width, height));
    return h;
}

bool BarcodeApplication::handleSubHeaderTouch(uint16_t x, uint16_t y, View backTarget, bool hasBack) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    if (hasBack && subHeaderBackRect(width, height).contains(x, y, kTouchPad)) {
        view_ = backTarget;
        redrawView(backTarget);
        return true;
    }
    if (subHeaderToggleRect(width, height).contains(x, y, kTouchPad)) {
        toggleTheme();
        return true;
    }
    return false;
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

void BarcodeApplication::updateGatewayLinkStatus(const esplink::GatewayLinkInfo& status) {
    gatewayLinkStatus_ = status;
    if (view_ != View::Settings) return;
    const uint32_t now = millis();
    if (now - gatewayLinkRedrawAt_ < 1000) return;
    gatewayLinkRedrawAt_ = now;
    drawSettings();
}

bool BarcodeApplication::consumeGatewayPingRequest() {
    if (!gatewayPingRequested_) return false;
    gatewayPingRequested_ = false;
    return true;
}

void BarcodeApplication::updateTrustPairingStatus(bool discovering, const std::string& peerFingerprint,
                                                  uint32_t numericCode, bool committed, bool cancelled) {
    // pairingStatus() only populates peerFingerprint/numericCode while AwaitingApproval -- once
    // the attempt reaches Committed/Cancelled they come back empty/0 again, which is exactly
    // what makes drawTrust()'s condition (trustDiscovering_ || !trustPeerFingerprint_.empty())
    // fall back to the idle pair-button+list layout on its own. Only redraw when something
    // actually changed, so the Trust screen isn't fully repainted every loop() tick.
    const bool changed = discovering != trustDiscovering_ || peerFingerprint != trustPeerFingerprint_ ||
                         numericCode != trustNumericCode_ || committed != trustCommitted_ ||
                         cancelled != trustCancelled_;
    trustDiscovering_ = discovering;
    trustPeerFingerprint_ = peerFingerprint;
    trustNumericCode_ = numericCode;
    trustCommitted_ = committed;
    trustCancelled_ = cancelled;
    if (changed && view_ == View::Trust) drawTrust();
}

void BarcodeApplication::updateGatewayRelayTrustedPeers(const std::vector<std::string>& fingerprints,
                                                        const std::vector<std::array<uint8_t, 6>>& macs) {
    gatewayRelayTrustedPeers_.clear();
    for (std::size_t i = 0; i < fingerprints.size(); ++i) {
        gatewayRelayTrustedPeers_.push_back(TrustPeerRow{fingerprints[i], macs[i]});
    }
}

void BarcodeApplication::updateEspNowTrustedPeers(const std::vector<std::string>& fingerprints,
                                                  const std::vector<std::array<uint8_t, 6>>& macs) {
    espNowTrustedPeers_.clear();
    for (std::size_t i = 0; i < fingerprints.size(); ++i) {
        espNowTrustedPeers_.push_back(TrustPeerRow{fingerprints[i], macs[i]});
    }
}

bool BarcodeApplication::consumeTrustPairRequest(std::array<uint8_t, 6>& outTargetMac) {
    if (!trustPairRequested_) return false;
    trustPairRequested_ = false;
    outTargetMac = trustPairTargetMac_;
    return true;
}

bool BarcodeApplication::consumeTrustConfirmRequest() {
    if (!trustConfirmRequested_) return false;
    trustConfirmRequested_ = false;
    return true;
}

bool BarcodeApplication::consumeTrustDenyRequest() {
    if (!trustDenyRequested_) return false;
    trustDenyRequested_ = false;
    return true;
}

bool BarcodeApplication::consumeTrustForgetRequest(std::string& outFingerprint) {
    if (!trustForgetRequested_) return false;
    trustForgetRequested_ = false;
    outFingerprint = trustForgetFingerprint_;
    return true;
}

bool BarcodeApplication::consumeSecurePairingToggleRequest(bool& outValue) {
    if (!securePairingToggleRequested_) return false;
    securePairingToggleRequested_ = false;
    outValue = securePairingToggleValue_;
    securePairingEnabled_ = securePairingToggleValue_;  // optimistic UI update; already applied at
                                                          // tap time in handleSettingsTouch, so this
                                                          // just keeps main.cpp's corrective path simple
    return true;
}

void BarcodeApplication::rebootDevice() {
    // Gateway relay mode is a one-way switch (main.cpp never reverts `active` back to
    // ActiveTransport::Legacy), so a restart is the only in-device way back to the normal ESP
    // Barcode Lab screen -- same sequence as the existing device.reboot protocol command
    // (EspIdfDeviceControl::reboot).
    Serial.flush();
    delay(100);
    ESP.restart();
}

void BarcodeApplication::drawHome() {
    applyOrientationForView(View::Home);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();

    tft_.fillScreen(th.bg);

    if (gatewayModeActive_) {
        const int16_t h = homeTopBarHeight(width, height);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(th.text, th.bg);
        tft_.drawString("Gateway Mode", static_cast<int16_t>(width / 2), static_cast<int16_t>(h / 2), 4);
        drawThemeSwitch(subHeaderToggleRect(width, height));
        drawGatewayHomeBanner();
        return;
    }

    const Rect chip = homeChipRect(width, height);
    tft_.fillRoundRect(chip.x, chip.y, chip.w, chip.h, static_cast<int16_t>(chip.h / 2), th.surface);
    tft_.drawRoundRect(chip.x, chip.y, chip.w, chip.h, static_cast<int16_t>(chip.h / 2), th.hairline);
    iconBarcode(tft_, static_cast<int16_t>(chip.x + 15), static_cast<int16_t>(chip.y + chip.h / 2), th.textMuted, th.surface);
    tft_.setTextDatum(ML_DATUM);
    tft_.setTextColor(th.text, th.surface);
    tft_.drawString(clipped(displayName(spec_.type), 15).c_str(), static_cast<int16_t>(chip.x + 28),
                    static_cast<int16_t>(chip.y + chip.h / 2), 2);
    iconChevronDown(tft_, static_cast<int16_t>(chip.x + chip.w - 12), static_cast<int16_t>(chip.y + chip.h / 2),
                    th.textFaint, th.surface);

    const Rect toggle = homeToggleRect(width, height);
    drawThemeSwitch(toggle);
    const Rect saveIcon = homeSaveIconRect(width, height);
    tft_.fillRoundRect(saveIcon.x, saveIcon.y, saveIcon.w, saveIcon.h, 7, th.surface);
    tft_.drawRoundRect(saveIcon.x, saveIcon.y, saveIcon.w, saveIcon.h, 7, th.hairline);
    iconBookmark(tft_, static_cast<int16_t>(saveIcon.x + saveIcon.w / 2), static_cast<int16_t>(saveIcon.y + saveIcon.h / 2),
                th.textMuted, th.surface);

    char countBuf[16];
    std::snprintf(countBuf, sizeof(countBuf), "%uB", static_cast<unsigned>(spec_.data.size()));
    tft_.setTextDatum(MR_DATUM);
    tft_.setTextColor(th.textMuted, th.bg);
    tft_.drawString(countBuf, static_cast<int16_t>(saveIcon.x - 8), static_cast<int16_t>(toggle.y + toggle.h / 2), 1);

    const Rect dataBox = homeDataBoxRect(width, height);
    tft_.fillRoundRect(dataBox.x, dataBox.y, dataBox.w, dataBox.h, 12, th.surface);
    tft_.drawRoundRect(dataBox.x, dataBox.y, dataBox.w, dataBox.h, 12, th.hairline);
    drawDataPreview();

    const auto actionRow = homeActionRow(width, height);
    static const char* labels[5] = {"OPTIONS", "PRESETS", "DISPLAY", "RANDOM", "SETTINGS"};
    tft_.drawFastHLine(actionRow[0].x, actionRow[0].y, static_cast<int16_t>(width - 2 * actionRow[0].x), th.hairline);
    tft_.drawFastHLine(actionRow[0].x, static_cast<int16_t>(actionRow[0].y + actionRow[0].h),
                       static_cast<int16_t>(width - 2 * actionRow[0].x), th.hairline);
    for (int i = 0; i < 5; ++i) {
        const Rect& cell = actionRow[static_cast<std::size_t>(i)];
        if (i > 0) tft_.drawFastVLine(cell.x, cell.y, cell.h, th.hairline);
        const bool primary = (i == 2);
        const uint16_t iconColor = primary ? th.accent : th.textMuted;
        const int16_t iconCx = static_cast<int16_t>(cell.x + cell.w / 2);
        const int16_t iconCy = static_cast<int16_t>(cell.y + cell.h / 2 - 6);
        switch (i) {
            case 0: iconSliders(tft_, iconCx, iconCy, iconColor, th.bg); break;
            case 1: iconList(tft_, iconCx, iconCy, iconColor, th.bg); break;
            case 2: iconEye(tft_, iconCx, iconCy, iconColor, th.bg); break;
            case 3: iconShuffle(tft_, iconCx, iconCy, iconColor, th.bg); break;
            case 4: iconGear(tft_, iconCx, iconCy, iconColor, th.bg); break;
            default: break;
        }
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(iconColor, th.bg);
        tft_.drawString(labels[static_cast<std::size_t>(i)], iconCx, static_cast<int16_t>(cell.y + cell.h - 8), 1);
    }

    drawStatus();
    drawKeyboard();
}

void BarcodeApplication::drawGatewayHomeBanner() {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();
    const int16_t cx = static_cast<int16_t>(width / 2);
    const int16_t iconCy = gatewayBannerIconCy(width, height);

    tft_.fillCircle(cx, iconCy, 27, th.surface);
    tft_.drawCircle(cx, iconCy, 27, th.hairline);
    iconShuffle(tft_, cx, iconCy, th.accent, th.surface);

    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(th.text, th.bg);
    tft_.drawString("Relaying USB & ESP-NOW", cx, static_cast<int16_t>(iconCy + 42), 2);

    tft_.setTextColor(th.textMuted, th.bg);
    const std::size_t maxChars = static_cast<std::size_t>(std::max(20, (width - 40) / 6));
    tft_.drawString(clipped(status_, maxChars).c_str(), cx, static_cast<int16_t>(iconCy + 62), 1);

    const auto buttons = gatewayBannerButtons(width, height);
    drawButton(buttons[0], "VIEW STATS", true);
    drawButton(buttons[1], "RESTART", false, theme().danger);
}

void BarcodeApplication::drawDataPreview() {
    const Theme& th = theme();
    const Rect box = homeDataBoxRect(tft_.width(), tft_.height());
    tft_.fillRoundRect(static_cast<int16_t>(box.x + 1), static_cast<int16_t>(box.y + 1),
                       static_cast<int16_t>(box.w - 2), static_cast<int16_t>(box.h - 2), 11, th.surface);

    if (spec_.data.empty()) {
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(th.textFaint, th.surface);
        tft_.drawString("Type to preview your data...", static_cast<int16_t>(box.x + 10),
                        static_cast<int16_t>(box.y + box.h / 2), 2);
        return;
    }

    const std::string text = printablePreview(spec_.data);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(th.text, th.surface);
    const std::size_t charsPerLine = static_cast<std::size_t>(std::max(8, (box.w - 44) / 9));
    const std::size_t lines = static_cast<std::size_t>(std::max(1, (box.h - 11) / 17));
    std::size_t start = 0;
    if (text.size() > charsPerLine * lines) start = text.size() - charsPerLine * lines;
    for (std::size_t line = 0; line < lines; ++line) {
        const std::size_t pos = start + line * charsPerLine;
        if (pos >= text.size()) break;
        tft_.drawString(text.substr(pos, charsPerLine).c_str(),
                        static_cast<int16_t>(box.x + 10),
                        static_cast<int16_t>(box.y + 7 + static_cast<int>(line) * 17),
                        2);
    }

    const Rect clearBtn = homeClearButtonRect(tft_.width(), tft_.height());
    iconClose(tft_, static_cast<int16_t>(clearBtn.x + clearBtn.w / 2), static_cast<int16_t>(clearBtn.y + clearBtn.h / 2),
             th.textFaint, th.surface);
}

void BarcodeApplication::drawStatus() {
    const Theme& th = theme();
    const Rect rect = homeStatusRect(tft_.width(), tft_.height());
    tft_.fillRect(rect.x, rect.y, rect.w, rect.h, th.bg);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(th.textFaint, th.bg);
    const std::size_t maxChars = static_cast<std::size_t>(std::max(16, rect.w / 6));
    tft_.drawString(clipped(status_, maxChars).c_str(), rect.x + 1, rect.y, 1);
}

void BarcodeApplication::setStatus(const std::string& status, bool redraw) {
    status_ = status;
    if (redraw && view_ == View::Home) drawStatus();
}

void BarcodeApplication::drawKeyboard() {
    const Theme& th = theme();
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
            drawButton(rect, std::string(1, keys[static_cast<std::size_t>(i)]), false, th.surfaceAlt);
        }
    }

    const int16_t controlY = static_cast<int16_t>(metrics.top + 4 * metrics.rowHeight);
    const int16_t controlH = static_cast<int16_t>(metrics.rowHeight - 1);
    const ControlRowLayout control = controlRowLayout(width, static_cast<int16_t>(controlY + 1), controlH);
    drawButton(control.keys[0],
               keyboardPage_ == KeyboardPage::Symbols
                   ? "FNC1"
                   : (keyboardPage_ == KeyboardPage::Upper ? "abc" : "ABC"),
               false, th.surfaceAlt);
    drawButton(control.keys[1], "SYM", false, th.surfaceAlt);
    drawButton(control.keys[2], "", false, th.surfaceAlt);
    drawButton(control.keys[3], "", false, th.surfaceAlt);
    iconBackspace(tft_, static_cast<int16_t>(control.keys[3].x + control.keys[3].w / 2),
                 static_cast<int16_t>(control.keys[3].y + control.keys[3].h / 2), th.text, th.surfaceAlt);
    drawButton(control.keys[4], "GO", true);
}

void BarcodeApplication::handleHomeTouch(uint16_t x, uint16_t y) {
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();

    if (gatewayModeActive_) {
        if (subHeaderToggleRect(width, height).contains(x, y, kTouchPad)) {
            toggleTheme();
            return;
        }
        const auto buttons = gatewayBannerButtons(width, height);
        if (buttons[0].contains(x, y, kTouchPad)) {
            view_ = View::Gateway;
            gatewayStatsRedrawAt_ = 0;  // force an immediate draw rather than waiting for the next throttled refresh
            drawGateway();
        } else if (buttons[1].contains(x, y, kTouchPad)) {
            rebootDevice();
        }
        return;
    }

    if (homeToggleRect(width, height).contains(x, y, kTouchPad)) {
        toggleTheme();
        return;
    }
    if (homeChipRect(width, height).contains(x, y, kTouchPad)) {
        view_ = View::TypePicker;
        drawTypePicker();
        return;
    }
    if (homeSaveIconRect(width, height).contains(x, y, kTouchPad)) {
        const std::string slot = presets_.nextSlotName();
        std::string error;
        if (slot.empty()) setStatus("All 32 preset slots are occupied");
        else if (presets_.save(slot, spec_, error)) setStatus("Saved as " + slot);
        else setStatus("Save failed: " + error);
        return;
    }
    if (!spec_.data.empty() && homeClearButtonRect(width, height).contains(x, y, kTouchPad)) {
        spec_.data.clear();
        setStatus("Payload cleared", false);
        drawDataPreview();
        drawStatus();
        return;
    }

    const auto actionRow = homeActionRow(width, height);
    const KeyboardMetrics metrics = keyboardMetrics(width, height);

    if (actionRow[0].contains(x, y, kTouchPad)) {
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
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader("Symbology");

    const auto& types = supportedTypes();
    const TypeGrid grid = typeGridMetrics(width, height, static_cast<int16_t>(contentTop + 4),
                                          static_cast<int>(types.size()));
    for (std::size_t i = 0; i < types.size(); ++i) {
        const int column = static_cast<int>(i) % grid.columns;
        const int row = static_cast<int>(i) / grid.columns;
        Rect rect{static_cast<int16_t>(grid.left + column * grid.colStride),
                  static_cast<int16_t>(grid.top + row * grid.rowStride),
                  grid.cellW, grid.cellH};
        const bool selected = types[i] == spec_.type;
        const uint16_t fill = selected ? th.accent : th.surface;
        const uint16_t border = selected ? th.accent : th.hairline;
        tft_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 10, fill);
        tft_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 10, border);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(selected ? th.accentText : th.text, fill);
        tft_.drawString(displayName(types[i]).c_str(), static_cast<int16_t>(rect.x + rect.w / 2),
                        static_cast<int16_t>(rect.y + rect.h / 2 - 7), 2);
        tft_.setTextColor(selected ? th.accentText : th.textMuted, fill);
        tft_.drawString(symbologyHint(types[i]).c_str(), static_cast<int16_t>(rect.x + rect.w / 2),
                        static_cast<int16_t>(rect.y + rect.h / 2 + 9), 1);
    }
}

void BarcodeApplication::handleTypeTouch(uint16_t x, uint16_t y) {
    if (handleSubHeaderTouch(x, y, View::Home)) return;
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const auto& types = supportedTypes();
    const TypeGrid grid = typeGridMetrics(width, height,
                                          static_cast<int16_t>(subHeaderHeight(width, height) + 4),
                                          static_cast<int>(types.size()));
    if (y < static_cast<uint16_t>(grid.top)) return;
    const int rawColumn = static_cast<int>(x) - grid.left;
    if (rawColumn < 0) return;
    const int column = std::min(grid.columns - 1, rawColumn / grid.colStride);
    const int row = (static_cast<int>(y) - grid.top) / grid.rowStride;
    const std::size_t index = static_cast<std::size_t>(row * grid.columns + column);
    if (index < types.size()) selectType(index);
}

void BarcodeApplication::selectType(std::size_t index) {
    spec_.type = supportedTypes()[index];
    showHome("Selected " + displayName(spec_.type));
}

void BarcodeApplication::drawOptions() {
    applyOrientationForView(View::Options);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader("Options");

    const std::array<std::pair<std::string, std::string>, 9> rows = {{
        {"Error Correction", toString(spec_.ecc)},
        {"Content Rotation", toString(spec_.rotation)},
        {"Quiet Zone", spec_.quietZone < 0 ? "default" : std::to_string(spec_.quietZone)},
        {"Min Module", std::to_string(spec_.minModulePixels) + " px"},
        {"DM Shape", spec_.dataMatrixRectangular ? "rectangle" : "square"},
        {"Invert Colors", spec_.invert ? "on" : "off"},
        {"Checksum", spec_.checksum ? "on" : "off"},
        {"Aztec Security", std::to_string(spec_.aztecSecurityPercent) + "%"},
        {"Aztec Layers", std::to_string(spec_.aztecMinLayers)}
    }};
    static constexpr bool kIsSwitchRow[9] = {false, false, false, false, false, true, true, false, false};

    const int16_t footerH = optionsFooterHeight(width, height);
    const Rect card = optionsListCardRect(width, height, contentTop, footerH);
    tft_.fillRoundRect(card.x, card.y, card.w, card.h, 12, th.surface);
    tft_.drawRoundRect(card.x, card.y, card.w, card.h, 12, th.hairline);
    const int16_t rowH = static_cast<int16_t>(card.h / 9);
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const int16_t y = static_cast<int16_t>(card.y + row * rowH);
        if (row > 0) tft_.drawFastHLine(static_cast<int16_t>(card.x + 2), y, static_cast<int16_t>(card.w - 4), th.hairline);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(th.text, th.surface);
        tft_.drawString(rows[static_cast<std::size_t>(row)].first.c_str(), static_cast<int16_t>(card.x + 12),
                        static_cast<int16_t>(y + rowH / 2), 2);
        if (kIsSwitchRow[row]) {
            const bool on = row == 5 ? spec_.invert : spec_.checksum;
            const Rect sw{static_cast<int16_t>(card.x + card.w - 42), static_cast<int16_t>(y + rowH / 2 - 9), 30, 18};
            drawMiniSwitch(sw, on);
        } else {
            tft_.setTextDatum(MR_DATUM);
            tft_.setTextColor(th.textMuted, th.surface);
            const std::string valueText = "<  " + rows[static_cast<std::size_t>(row)].second + "  >";
            tft_.drawString(valueText.c_str(), static_cast<int16_t>(card.x + card.w - 14), static_cast<int16_t>(y + rowH / 2), 2);
        }
    }

    drawButton(optionsDisplayButtonRect(width, height), "DISPLAY", true);
}

void BarcodeApplication::handleOptionsTouch(uint16_t x, uint16_t y) {
    if (handleSubHeaderTouch(x, y, View::Home)) return;
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();

    if (optionsDisplayButtonRect(width, height).contains(x, y, kTouchPad)) {
        std::string error;
        if (!generate(spec_, true, error)) showHome("Error: " + error);
        return;
    }

    const int16_t contentTop = subHeaderHeight(width, height);
    const int16_t footerH = optionsFooterHeight(width, height);
    const Rect card = optionsListCardRect(width, height, contentTop, footerH);
    if (!card.contains(x, y, 0)) return;
    const int16_t rowH = static_cast<int16_t>(card.h / 9);
    const int row = (static_cast<int>(y) - card.y) / rowH;
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
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader(presetDeleteMode_ ? "Tap to Delete" : "Presets");

    const auto names = presets_.list();
    constexpr std::size_t pageSize = 8;
    const std::size_t pages = std::max<std::size_t>(1, (names.size() + pageSize - 1) / pageSize);
    if (presetPage_ >= pages) presetPage_ = pages - 1;
    const std::size_t start = presetPage_ * pageSize;

    const int16_t footerH = presetsFooterHeight(width, height);
    const Rect card = presetsListCardRect(width, height, contentTop, footerH);
    tft_.fillRoundRect(card.x, card.y, card.w, card.h, 12, th.surface);
    tft_.drawRoundRect(card.x, card.y, card.w, card.h, 12, th.hairline);
    const int16_t rowH = static_cast<int16_t>(card.h / static_cast<int16_t>(pageSize));
    for (std::size_t i = 0; i < pageSize; ++i) {
        const int16_t y = static_cast<int16_t>(card.y + static_cast<int>(i) * rowH);
        if (i > 0) tft_.drawFastHLine(static_cast<int16_t>(card.x + 2), y, static_cast<int16_t>(card.w - 4), th.hairline);
        const std::size_t index = start + i;
        tft_.setTextDatum(ML_DATUM);
        if (index < names.size()) {
            tft_.setTextColor(presetDeleteMode_ ? th.danger : th.text, th.surface);
            tft_.drawString(clipped(names[index], 26).c_str(), static_cast<int16_t>(card.x + 12), static_cast<int16_t>(y + rowH / 2), 2);
            if (presetDeleteMode_) {
                iconClose(tft_, static_cast<int16_t>(card.x + card.w - 16), static_cast<int16_t>(y + rowH / 2), th.danger, th.surface);
            }
        } else {
            tft_.setTextColor(th.textFaint, th.surface);
            tft_.drawString("--", static_cast<int16_t>(card.x + 12), static_cast<int16_t>(y + rowH / 2), 2);
        }
    }

    const auto footer = presetsFooterButtons(width, height);
    drawButton(footer[0], "SAVE");
    drawButton(footer[1], presetDeleteMode_ ? "CANCEL" : "DELETE", false, presetDeleteMode_ ? th.danger : th.surfaceAlt);
    tft_.fillRoundRect(footer[2].x, footer[2].y, footer[2].w, footer[2].h, 8, th.surface);
    tft_.drawRoundRect(footer[2].x, footer[2].y, footer[2].w, footer[2].h, 8, th.hairline);
    iconChevronLeft(tft_, static_cast<int16_t>(footer[2].x + 16), static_cast<int16_t>(footer[2].y + footer[2].h / 2), th.textMuted, th.surface);
    iconChevronRight(tft_, static_cast<int16_t>(footer[2].x + footer[2].w - 16), static_cast<int16_t>(footer[2].y + footer[2].h / 2), th.textMuted, th.surface);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(th.textMuted, th.surface);
    char pageBuf[20];
    std::snprintf(pageBuf, sizeof(pageBuf), "%u / %u", static_cast<unsigned>(presetPage_ + 1), static_cast<unsigned>(pages));
    tft_.drawString(pageBuf, static_cast<int16_t>(footer[2].x + footer[2].w / 2), static_cast<int16_t>(footer[2].y + footer[2].h / 2), 1);
}

void BarcodeApplication::handlePresetsTouch(uint16_t x, uint16_t y) {
    if (handleSubHeaderTouch(x, y, View::Home)) return;
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const auto footer = presetsFooterButtons(width, height);

    if (footer[0].contains(x, y, kTouchPad)) {
        const std::string slot = presets_.nextSlotName();
        std::string error;
        if (slot.empty()) status_ = "Preset slots full";
        else if (presets_.save(slot, spec_, error)) status_ = "Saved as " + slot;
        else status_ = "Save failed: " + error;
        drawPresets();
        return;
    }
    if (footer[1].contains(x, y, kTouchPad)) {
        presetDeleteMode_ = !presetDeleteMode_;
        drawPresets();
        return;
    }
    if (footer[2].contains(x, y, kTouchPad)) {
        const auto names = presets_.list();
        const std::size_t pages = std::max<std::size_t>(1, (names.size() + 7) / 8);
        const int16_t third = static_cast<int16_t>(footer[2].w / 3);
        if (x < static_cast<uint16_t>(footer[2].x + third)) {
            presetPage_ = presetPage_ == 0 ? pages - 1 : presetPage_ - 1;
        } else if (x >= static_cast<uint16_t>(footer[2].x + footer[2].w - third)) {
            presetPage_ = (presetPage_ + 1) % pages;
        }
        drawPresets();
        return;
    }

    const int16_t contentTop = subHeaderHeight(width, height);
    const int16_t footerH = presetsFooterHeight(width, height);
    const Rect card = presetsListCardRect(width, height, contentTop, footerH);
    if (!card.contains(x, y, 0)) return;
    const int16_t rowH = static_cast<int16_t>(card.h / 8);
    const int row = (static_cast<int>(y) - card.y) / rowH;
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
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader("Settings");

    const std::array<std::pair<const char*, ScreenOrientation>, 2> rows = {{
        {"Barcode Orientation", config_.barcodeOrientation()},
        {"Editor Orientation", config_.editorOrientation()},
    }};
    const Rect card = settingsCardRect(width, height, contentTop);
    tft_.fillRoundRect(card.x, card.y, card.w, card.h, 12, th.surface);
    tft_.drawRoundRect(card.x, card.y, card.w, card.h, 12, th.hairline);
    const int16_t rowH = static_cast<int16_t>(card.h / 3);
    for (int row = 0; row < 2; ++row) {
        const int16_t y = static_cast<int16_t>(card.y + row * rowH);
        if (row > 0) tft_.drawFastHLine(static_cast<int16_t>(card.x + 2), y, static_cast<int16_t>(card.w - 4), th.hairline);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(th.text, th.surface);
        tft_.drawString(rows[static_cast<std::size_t>(row)].first, static_cast<int16_t>(card.x + 12), static_cast<int16_t>(y + rowH / 2), 2);
        tft_.setTextDatum(MR_DATUM);
        tft_.setTextColor(th.textMuted, th.surface);
        const std::string label =
            std::string("<  ") + orientationLabel(rows[static_cast<std::size_t>(row)].second) + " deg  >";
        tft_.drawString(label.c_str(), static_cast<int16_t>(card.x + card.w - 14), static_cast<int16_t>(y + rowH / 2), 2);
    }

    // Row 3: Secure Pairing toggle -- reuses drawMiniSwitch, already used for Options' switch
    // rows (Invert Colors/Checksum). See consumeSecurePairingToggleRequest for how a tap here
    // flows through to whichever of EspNowEndpoint's/GatewayRelay's TrustConfigStore is active.
    {
        const int16_t y = static_cast<int16_t>(card.y + 2 * rowH);
        tft_.drawFastHLine(static_cast<int16_t>(card.x + 2), y, static_cast<int16_t>(card.w - 4), th.hairline);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(th.text, th.surface);
        tft_.drawString("Secure Pairing", static_cast<int16_t>(card.x + 12), static_cast<int16_t>(y + rowH / 2), 2);
        const Rect sw{static_cast<int16_t>(card.x + card.w - 42), static_cast<int16_t>(y + rowH / 2 - 9), 30, 18};
        drawMiniSwitch(sw, securePairingEnabled_);
    }

    // Gateway relay mode has its own live stats screen (View::Gateway) for this board's own
    // radio role; this row is the complementary "am I near a gateway?" indicator for a board
    // running as a plain client, fed by EspNowEndpoint's discovery ping/pong (see main.cpp).
    if (!gatewayModeActive_) {
        const Rect linkRow = gatewayLinkStatusRowRect(width, static_cast<int16_t>(card.y + card.h));
        tft_.fillRoundRect(linkRow.x, linkRow.y, linkRow.w, linkRow.h, 10, th.surface);
        tft_.drawRoundRect(linkRow.x, linkRow.y, linkRow.w, linkRow.h, 10, th.hairline);
        tft_.fillCircle(static_cast<int16_t>(linkRow.x + 16), static_cast<int16_t>(linkRow.y + linkRow.h / 2), 4,
                        gatewayLinkStatus_.connected ? th.accent : th.textFaint);
        tft_.setTextDatum(ML_DATUM);
        tft_.setTextColor(th.text, th.surface);
        std::string label;
        if (gatewayLinkStatus_.connected) {
            label = "ESP-NOW Gateway: connected";
            if (gatewayLinkStatus_.rttMs > 0) label += " (" + std::to_string(gatewayLinkStatus_.rttMs) + "ms)";
        } else if (!gatewayLinkStatus_.gatewayId.empty()) {
            label = "ESP-NOW Gateway: lost";
        } else {
            label = "ESP-NOW Gateway: searching...";
        }
        tft_.drawString(label.c_str(), static_cast<int16_t>(linkRow.x + 28), static_cast<int16_t>(linkRow.y + linkRow.h / 2), 1);
    }

    // "Trust" navigation button below whatever else this screen rendered -- same accent-button
    // shape as drawGatewayHomeBanner's button-to-View::Gateway pattern.
    drawButton(settingsTrustButtonRect(width, card, gatewayModeActive_), "TRUST", true);
}

void BarcodeApplication::handleSettingsTouch(uint16_t x, uint16_t y) {
    if (handleSubHeaderTouch(x, y, View::Home)) return;
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const int16_t contentTop = subHeaderHeight(width, height);
    const Rect card = settingsCardRect(width, height, contentTop);

    if (settingsTrustButtonRect(width, card, gatewayModeActive_).contains(x, y, kTouchPad)) {
        view_ = View::Trust;
        drawTrust();
        return;
    }

    if (!card.contains(x, y, 0)) return;
    const int16_t rowH = static_cast<int16_t>(card.h / 3);
    const int row = (static_cast<int>(y) - card.y) / rowH;
    if (row < 0 || row > 2) return;

    if (row == 2) {
        securePairingToggleValue_ = !securePairingEnabled_;
        securePairingToggleRequested_ = true;
        securePairingEnabled_ = securePairingToggleValue_;  // optimistic UI update; main.cpp
                                                              // corrects it if persistence fails
        drawSettings();
        return;
    }

    const int direction = x < width / 2 ? -1 : 1;
    const OrientationTarget target = row == 0 ? OrientationTarget::Barcode : OrientationTarget::Editor;
    const ScreenOrientation current = row == 0 ? config_.barcodeOrientation() : config_.editorOrientation();
    const int next = (static_cast<int>(current) + direction + 4) % 4;
    setOrientation(target, static_cast<ScreenOrientation>(next));
}

void BarcodeApplication::drawGateway() {
    applyOrientationForView(View::Gateway);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader("Gateway");

    const esplink::GatewayStats::Snapshot& link = gatewayStats_.linkStats;
    const Rect pill = gatewayStatusPillRect(contentTop);
    tft_.fillRoundRect(pill.x, pill.y, pill.w, pill.h, static_cast<int16_t>(pill.h / 2), th.surface);
    tft_.drawRoundRect(pill.x, pill.y, pill.w, pill.h, static_cast<int16_t>(pill.h / 2), th.hairline);
    tft_.fillCircle(static_cast<int16_t>(pill.x + 14), static_cast<int16_t>(pill.y + pill.h / 2), 4,
                    link.hostConnected ? th.accent : th.danger);
    tft_.setTextDatum(ML_DATUM);
    tft_.setTextColor(th.text, th.surface);
    const char* hostLabel = !link.hostEverSeen ? "NO HOST YET" : link.hostConnected ? "USB HOST CONNECTED" : "USB HOST LOST";
    tft_.drawString(hostLabel, static_cast<int16_t>(pill.x + 24), static_cast<int16_t>(pill.y + pill.h / 2), 1);

    const Rect pingButton = gatewayPingButtonRect(contentTop, width);
    tft_.fillRoundRect(pingButton.x, pingButton.y, pingButton.w, pingButton.h, static_cast<int16_t>(pingButton.h / 2),
                       th.accent);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(th.accentText, th.accent);
    tft_.drawString("PING", static_cast<int16_t>(pingButton.x + pingButton.w / 2),
                    static_cast<int16_t>(pingButton.y + pingButton.h / 2), 1);

    const int16_t statsY = static_cast<int16_t>(pill.y + pill.h + 6);
    const auto tiles = gatewayStatTiles(width, statsY);
    const std::array<std::pair<std::string, const char*>, 3> stats = {{
        {std::to_string(link.peerCount), "PEERS"},
        {std::to_string(gatewayStats_.usbToEspNowMessageCount), "SENT"},
        {std::to_string(gatewayStats_.espNowToUsbMessageCount), "RECEIVED"},
    }};
    for (int i = 0; i < 3; ++i) {
        const Rect& tile = tiles[static_cast<std::size_t>(i)];
        tft_.fillRoundRect(tile.x, tile.y, tile.w, tile.h, 8, th.surface);
        tft_.drawRoundRect(tile.x, tile.y, tile.w, tile.h, 8, th.hairline);
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.text, th.surface);
        tft_.drawString(stats[static_cast<std::size_t>(i)].first.c_str(), static_cast<int16_t>(tile.x + 8), static_cast<int16_t>(tile.y + 5), 2);
        tft_.setTextColor(th.textMuted, th.surface);
        tft_.drawString(stats[static_cast<std::size_t>(i)].second, static_cast<int16_t>(tile.x + 8), static_cast<int16_t>(tile.y + tile.h - 14), 1);
    }

    const Rect peersCard = gatewayPeersCardRect(width, height, statsY);
    tft_.fillRoundRect(peersCard.x, peersCard.y, peersCard.w, peersCard.h, 10, th.surface);
    tft_.drawRoundRect(peersCard.x, peersCard.y, peersCard.w, peersCard.h, 10, th.hairline);
    const int16_t peerRowH = 16;
    std::size_t shown = 0;
    int16_t y = static_cast<int16_t>(peersCard.y + 4);
    for (; shown < link.peerCount; ++shown) {
        if (y + peerRowH > peersCard.y + peersCard.h - 4) break;
        const auto& peer = link.peers[shown];
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.text, th.surface);
        const std::string label =
            peer.deviceId[0] != '\0' ? std::string(peer.deviceIdCStr()) : formatMac(peer.mac);
        tft_.drawString(label.c_str(), static_cast<int16_t>(peersCard.x + 8), static_cast<int16_t>(y + 2), 1);
        tft_.setTextDatum(TR_DATUM);
        tft_.setTextColor(th.textMuted, th.surface);
        std::string right = formatAgeSeconds(link.nowMs, peer.lastSeenMs);
        if (peer.everPinged) right += " ~" + std::to_string(peer.lastRttMs) + "ms";
        tft_.drawString(right.c_str(), static_cast<int16_t>(peersCard.x + peersCard.w - 8), static_cast<int16_t>(y + 2), 1);
        y = static_cast<int16_t>(y + peerRowH);
    }
    if (link.peerCount == 0) {
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.textFaint, th.surface);
        tft_.drawString("No peers connected yet", static_cast<int16_t>(peersCard.x + 8), static_cast<int16_t>(peersCard.y + 8), 1);
    } else if (shown < link.peerCount) {
        char more[24];
        std::snprintf(more, sizeof(more), "+%u more not shown", static_cast<unsigned>(link.peerCount - shown));
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.textFaint, th.surface);
        tft_.drawString(more, static_cast<int16_t>(peersCard.x + 8), static_cast<int16_t>(y + 2), 1);
    }

    const Rect restart = gatewayRestartButtonRect(width, height);
    tft_.fillRoundRect(restart.x, restart.y, restart.w, restart.h, 10, th.danger);
    tft_.drawRoundRect(restart.x, restart.y, restart.w, restart.h, 10, th.danger);
    iconRestart(tft_, static_cast<int16_t>(restart.x + restart.w / 2 - 46), static_cast<int16_t>(restart.y + restart.h / 2), TFT_WHITE, th.danger);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(TFT_WHITE, th.danger);
    tft_.drawString("RESTART DEVICE", static_cast<int16_t>(restart.x + restart.w / 2 + 8), static_cast<int16_t>(restart.y + restart.h / 2), 2);
}

void BarcodeApplication::handleGatewayTouch(uint16_t x, uint16_t y) {
    if (handleSubHeaderTouch(x, y, View::Home)) return;
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    if (gatewayRestartButtonRect(width, height).contains(x, y, kTouchPad)) {
        rebootDevice();
        return;
    }
    const int16_t contentTop = subHeaderHeight(width, height);
    if (gatewayPingButtonRect(contentTop, width).contains(x, y, kTouchPad)) {
        gatewayPingRequested_ = true;
    }
}

void BarcodeApplication::drawTrust() {
    applyOrientationForView(View::Trust);
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const Theme& th = theme();

    tft_.fillScreen(th.bg);
    const int16_t contentTop = drawSubHeader("Trust");

    if (trustDiscovering_ || !trustPeerFingerprint_.empty()) {
        const Rect card = settingsCardRect(width, height, contentTop);
        tft_.fillRoundRect(card.x, card.y, card.w, card.h, 12, th.surface);
        tft_.drawRoundRect(card.x, card.y, card.w, card.h, 12, th.hairline);
        tft_.setTextDatum(TC_DATUM);
        tft_.setTextColor(th.text, th.surface);
        const std::string title = trustDiscovering_ ? "Waiting for peer..." : ("Pair with " + trustPeerFingerprint_ + "?");
        tft_.drawString(title.c_str(), static_cast<int16_t>(card.x + card.w / 2), static_cast<int16_t>(card.y + 16), 2);
        if (!trustDiscovering_) {
            char code[8];
            std::snprintf(code, sizeof(code), "%06u", static_cast<unsigned>(trustNumericCode_));
            tft_.setTextDatum(MC_DATUM);
            tft_.drawString(code, static_cast<int16_t>(card.x + card.w / 2), static_cast<int16_t>(card.y + card.h / 2), 4);

            const Rect confirmBtn{static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + card.h - 44),
                                 static_cast<int16_t>(card.w / 2 - 18), 32};
            const Rect denyBtn{static_cast<int16_t>(card.x + card.w / 2 + 6), static_cast<int16_t>(card.y + card.h - 44),
                              static_cast<int16_t>(card.w / 2 - 18), 32};
            tft_.fillRoundRect(confirmBtn.x, confirmBtn.y, confirmBtn.w, confirmBtn.h, 8, th.accent);
            tft_.setTextDatum(MC_DATUM);
            tft_.setTextColor(th.accentText, th.accent);
            tft_.drawString("CONFIRM", static_cast<int16_t>(confirmBtn.x + confirmBtn.w / 2),
                            static_cast<int16_t>(confirmBtn.y + confirmBtn.h / 2), 1);
            tft_.fillRoundRect(denyBtn.x, denyBtn.y, denyBtn.w, denyBtn.h, 8, th.danger);
            tft_.setTextColor(TFT_WHITE, th.danger);
            tft_.drawString("DENY", static_cast<int16_t>(denyBtn.x + denyBtn.w / 2),
                            static_cast<int16_t>(denyBtn.y + denyBtn.h / 2), 1);
        }
        return;
    }

    // "PAIR" (rather than a longer label) so it fits gatewayPingButtonRect's compact
    // top-right-corner shape, matching how the Gateway screen's identically-sized PING button
    // keeps its label short.
    const Rect pairButton = gatewayPingButtonRect(contentTop, width);
    tft_.fillRoundRect(pairButton.x, pairButton.y, pairButton.w, pairButton.h,
                       static_cast<int16_t>(pairButton.h / 2), th.accent);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(th.accentText, th.accent);
    tft_.drawString("PAIR", static_cast<int16_t>(pairButton.x + pairButton.w / 2),
                    static_cast<int16_t>(pairButton.y + pairButton.h / 2), 1);

    const int16_t listTop = static_cast<int16_t>(pairButton.y + pairButton.h + 8);
    const Rect listCard = gatewayPeersCardRect(width, height, listTop);
    tft_.fillRoundRect(listCard.x, listCard.y, listCard.w, listCard.h, 10, th.surface);
    tft_.drawRoundRect(listCard.x, listCard.y, listCard.w, listCard.h, 10, th.hairline);
    // One row per trusted peer with its fingerprint on the left and a "Forget" tap target on
    // the right, mirroring drawGateway()'s peer list. gatewayModeActive_ selects which of the
    // two BarcodeApplication-owned snapshots to read (see updateGatewayRelayTrustedPeers/
    // updateEspNowTrustedPeers).
    const std::vector<TrustPeerRow>& peers = gatewayModeActive_ ? gatewayRelayTrustedPeers_ : espNowTrustedPeers_;
    const int16_t peerRowH = 16;
    std::size_t shown = 0;
    int16_t y = static_cast<int16_t>(listCard.y + 4);
    for (; shown < peers.size(); ++shown) {
        if (y + peerRowH > listCard.y + listCard.h - 4) break;
        const TrustPeerRow& peer = peers[shown];
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.text, th.surface);
        tft_.drawString(peer.fingerprint.c_str(), static_cast<int16_t>(listCard.x + 8), static_cast<int16_t>(y + 2), 1);
        const Rect forgetBtn{static_cast<int16_t>(listCard.x + listCard.w - 56), y, 48, peerRowH};
        tft_.fillRoundRect(forgetBtn.x, forgetBtn.y, forgetBtn.w, forgetBtn.h, 4, th.danger);
        tft_.setTextDatum(MC_DATUM);
        tft_.setTextColor(TFT_WHITE, th.danger);
        tft_.drawString("X", static_cast<int16_t>(forgetBtn.x + forgetBtn.w / 2),
                        static_cast<int16_t>(forgetBtn.y + forgetBtn.h / 2), 1);
        y = static_cast<int16_t>(y + peerRowH);
    }
    if (peers.empty()) {
        tft_.setTextDatum(TL_DATUM);
        tft_.setTextColor(th.textFaint, th.surface);
        tft_.drawString("No paired devices yet", static_cast<int16_t>(listCard.x + 8),
                        static_cast<int16_t>(listCard.y + 8), 1);
    }
}

void BarcodeApplication::handleTrustTouch(uint16_t x, uint16_t y) {
    if (handleSubHeaderTouch(x, y, View::Home)) return;
    const uint16_t width = tft_.width();
    const uint16_t height = tft_.height();
    const int16_t contentTop = subHeaderHeight(width, height);

    if (trustDiscovering_ || !trustPeerFingerprint_.empty()) {
        if (trustDiscovering_) return;  // no buttons yet, just waiting on the peer's reply
        const Rect card = settingsCardRect(width, height, contentTop);
        const Rect confirmBtn{static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + card.h - 44),
                             static_cast<int16_t>(card.w / 2 - 18), 32};
        const Rect denyBtn{static_cast<int16_t>(card.x + card.w / 2 + 6), static_cast<int16_t>(card.y + card.h - 44),
                          static_cast<int16_t>(card.w / 2 - 18), 32};
        if (confirmBtn.contains(x, y, kTouchPad)) {
            trustConfirmRequested_ = true;
        } else if (denyBtn.contains(x, y, kTouchPad)) {
            trustDenyRequested_ = true;
        }
        return;
    }

    const Rect pairButton = gatewayPingButtonRect(contentTop, width);
    if (pairButton.contains(x, y, kTouchPad)) {
        // Target MAC resolution: in direct (non-gateway) mode, EspNowEndpoint is inherently 1:1
        // with a single gateway, so this offers the currently-discovered gateway (gatewayLinkStatus_,
        // fed by EspNowEndpoint's own discovery ping/pong). In gateway mode, GatewayRelay can fan
        // out to several clients, so this picks the first discovered peer (gatewayStats_.linkStats.peers)
        // not already in the trusted list.
        if (!gatewayModeActive_) {
            if (gatewayLinkStatus_.connected) {
                std::array<uint8_t, 6> mac{};
                if (macFromString(gatewayLinkStatus_.gatewayId, mac)) {
                    trustPairTargetMac_ = mac;
                    trustPairRequested_ = true;
                }
            }
        } else {
            for (std::size_t i = 0; i < gatewayStats_.linkStats.peerCount; ++i) {
                const auto& candidate = gatewayStats_.linkStats.peers[i];
                const bool alreadyTrusted =
                    std::any_of(gatewayRelayTrustedPeers_.begin(), gatewayRelayTrustedPeers_.end(),
                               [&](const TrustPeerRow& row) { return row.mac == candidate.mac; });
                if (!alreadyTrusted) {
                    trustPairTargetMac_ = candidate.mac;
                    trustPairRequested_ = true;
                    break;
                }
            }
        }
        return;
    }

    const int16_t listTop = static_cast<int16_t>(pairButton.y + pairButton.h + 8);
    const Rect listCard = gatewayPeersCardRect(width, height, listTop);
    if (!listCard.contains(x, y, 0)) return;

    const std::vector<TrustPeerRow>& peers = gatewayModeActive_ ? gatewayRelayTrustedPeers_ : espNowTrustedPeers_;
    const int16_t peerRowH = 16;
    const int row = (static_cast<int>(y) - listCard.y - 4) / peerRowH;
    if (row < 0 || static_cast<std::size_t>(row) >= peers.size()) return;
    const int16_t rowY = static_cast<int16_t>(listCard.y + 4 + row * peerRowH);
    const Rect forgetBtn{static_cast<int16_t>(listCard.x + listCard.w - 56), rowY, 48, peerRowH};
    if (forgetBtn.contains(x, y, kTouchPad)) {
        trustForgetFingerprint_ = peers[static_cast<std::size_t>(row)].fingerprint;
        trustForgetRequested_ = true;
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
