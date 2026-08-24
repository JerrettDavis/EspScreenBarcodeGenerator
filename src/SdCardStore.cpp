#include "SdCardStore.h"

#include <SD.h>
#include <SPI.h>

namespace {
constexpr int kSdCsPin = 5;
constexpr int kSdMosiPin = 23;
constexpr int kSdMisoPin = 19;
constexpr int kSdSckPin = 18;
constexpr uint32_t kSdFrequencyHz = 20000000;

// The TFT/touch controllers own the ESP32's HSPI bus (see platformio.ini); the microSD
// slot on this board is wired to the separate default VSPI pins, so it gets its own
// SPIClass instance rather than sharing TFT_eSPI's.
SPIClass sdSpi(VSPI);
}  // namespace

bool SdCardStore::begin(std::string& error) {
    sdSpi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    if (!SD.begin(kSdCsPin, sdSpi, kSdFrequencyHz) || SD.cardType() == CARD_NONE) {
        error = "no SD card detected";
        SD.end();
        mounted_ = false;
        return false;
    }
    mounted_ = true;
    return true;
}

uint64_t SdCardStore::totalBytes() const { return mounted_ ? SD.totalBytes() : 0; }
uint64_t SdCardStore::usedBytes() const { return mounted_ ? SD.usedBytes() : 0; }
