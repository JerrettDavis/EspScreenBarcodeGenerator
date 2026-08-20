#pragma once

#include <string>
#include <vector>

#include "EspBarcodeCore.h"

class PresetStore {
public:
    bool begin(std::string& error);
    bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error);
    bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const;
    bool remove(const std::string& name, std::string& error);
    std::vector<std::string> list() const;
    std::string nextSlotName() const;

    static bool validName(const std::string& name);

private:
    static std::string pathFor(const std::string& name);
};
