#pragma once

#include <FS.h>

#include <string>
#include <vector>

#include "EspBarcodeCore.h"

class PresetStore {
public:
    // `filesystem` must already be mounted (LittleFS.begin()/SdCardStore::begin() having
    // succeeded) by the time this is called -- PresetStore only owns the /presets
    // directory within whichever filesystem it's given, not the mount itself, so the
    // storage backend (internal flash vs SD card) is entirely the caller's choice.
    bool begin(std::string& error, fs::FS& filesystem);
    bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error);
    bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const;
    bool remove(const std::string& name, std::string& error);
    std::vector<std::string> list() const;
    std::string nextSlotName() const;

    static bool validName(const std::string& name);

private:
    static std::string pathFor(const std::string& name);

    fs::FS* fs_ = nullptr;
};
