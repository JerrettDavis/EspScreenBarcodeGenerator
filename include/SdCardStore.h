#pragma once

#include <cstdint>
#include <string>

// Mounts the board's onboard microSD slot. On the Hosyond/lcdwiki ESP32-32E 3.5" board
// family this project targets (see README's GPIO table), the card reader is wired to the
// ESP32's default VSPI pins -- CS 5 / MOSI 23 / MISO 19 / SCK 18 -- entirely separate from
// the ST7796/XPT2046 HSPI bus the display and touch controller share, so mounting it
// doesn't contend with TFT_eSPI.
class SdCardStore {
public:
    // Non-fatal: returns false (and mounted() stays false) if no card is present or it
    // can't be read -- callers fall back to LittleFS rather than failing boot.
    bool begin(std::string& error);
    bool mounted() const { return mounted_; }
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

private:
    bool mounted_ = false;
};
