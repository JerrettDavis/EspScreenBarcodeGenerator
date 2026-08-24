#include "PresetStore.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>

using namespace espbarcode;

namespace {
constexpr const char* kPresetDirectory = "/presets";
constexpr std::size_t kMaxPresets = 32;
}

bool PresetStore::begin(std::string& error, fs::FS& filesystem) {
    fs_ = &filesystem;
    if (!fs_->exists(kPresetDirectory) && !fs_->mkdir(kPresetDirectory)) {
        error = "could not create preset directory";
        return false;
    }
    return true;
}

bool PresetStore::validName(const std::string& name) {
    if (name.empty() || name.size() > 24) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::string PresetStore::pathFor(const std::string& name) {
    return std::string(kPresetDirectory) + "/" + name + ".json";
}

bool PresetStore::save(const std::string& name,
                       const BarcodeSpec& spec,
                       std::string& error) {
    if (!validName(name)) {
        error = "preset name must be 1-24 characters using A-Z, 0-9, dash, or underscore";
        return false;
    }
    const std::string path = pathFor(name);
    if (!fs_->exists(path.c_str()) && list().size() >= kMaxPresets) {
        error = "preset limit reached";
        return false;
    }

    JsonDocument document;
    document["schema"] = 1;
    document["type"] = toString(spec.type);
    document["data"] = spec.data;
    document["ecc"] = toString(spec.ecc);
    document["rotation"] = toString(spec.rotation);
    document["quiet"] = spec.quietZone;
    document["min_module"] = spec.minModulePixels;
    document["qr_min_version"] = spec.qrMinVersion;
    document["qr_max_version"] = spec.qrMaxVersion;
    document["aztec_security"] = spec.aztecSecurityPercent;
    document["aztec_layers"] = spec.aztecMinLayers;
    document["dm_rect"] = spec.dataMatrixRectangular;
    document["invert"] = spec.invert;
    document["checksum"] = spec.checksum;

    File file = fs_->open(path.c_str(), "w");
    if (!file) {
        error = "could not open preset for writing";
        return false;
    }
    const std::size_t written = serializeJson(document, file);
    file.close();
    if (written == 0) {
        fs_->remove(path.c_str());
        error = "could not serialize preset";
        return false;
    }
    return true;
}

bool PresetStore::load(const std::string& name,
                       BarcodeSpec& spec,
                       std::string& error) const {
    if (!validName(name)) {
        error = "invalid preset name";
        return false;
    }
    File file = fs_->open(pathFor(name).c_str(), "r");
    if (!file) {
        error = "preset not found";
        return false;
    }
    JsonDocument document;
    const DeserializationError parseError = deserializeJson(document, file);
    file.close();
    if (parseError) {
        error = std::string("invalid preset JSON: ") + parseError.c_str();
        return false;
    }

    Symbology type;
    ErrorCorrection ecc;
    Rotation rotation;
    const char* typeText = document["type"] | "";
    const char* eccText = document["ecc"] | "M";
    const char* rotationText = document["rotation"] | "auto";
    if (!tryParseSymbology(typeText, type) ||
        !tryParseErrorCorrection(eccText, ecc) ||
        !tryParseRotation(rotationText, rotation)) {
        error = "preset contains unsupported settings";
        return false;
    }

    BarcodeSpec loaded;
    loaded.type = type;
    loaded.data = std::string(document["data"] | "");
    loaded.ecc = ecc;
    loaded.rotation = rotation;
    loaded.quietZone = document["quiet"] | -1;
    loaded.minModulePixels = document["min_module"] | 2;
    loaded.qrMinVersion = document["qr_min_version"] | 1;
    loaded.qrMaxVersion = document["qr_max_version"] | 20;
    loaded.aztecSecurityPercent = document["aztec_security"] | 23;
    loaded.aztecMinLayers = document["aztec_layers"] | 1;
    loaded.dataMatrixRectangular = document["dm_rect"] | false;
    loaded.invert = document["invert"] | false;
    loaded.checksum = document["checksum"] | true;
    spec = std::move(loaded);
    return true;
}

bool PresetStore::remove(const std::string& name, std::string& error) {
    if (!validName(name)) {
        error = "invalid preset name";
        return false;
    }
    const std::string path = pathFor(name);
    if (!fs_->exists(path.c_str())) {
        error = "preset not found";
        return false;
    }
    if (!fs_->remove(path.c_str())) {
        error = "could not delete preset";
        return false;
    }
    return true;
}

std::vector<std::string> PresetStore::list() const {
    std::vector<std::string> names;
    File root = fs_->open(kPresetDirectory);
    if (!root || !root.isDirectory()) return names;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            std::string name = file.name();
            const std::size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) name.erase(0, slash + 1);
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                name.resize(name.size() - 5);
                if (validName(name)) names.push_back(std::move(name));
            }
        }
        file = root.openNextFile();
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string PresetStore::nextSlotName() const {
    const auto names = list();
    for (int i = 1; i <= static_cast<int>(kMaxPresets); ++i) {
        char candidate[12];
        std::snprintf(candidate, sizeof(candidate), "SLOT%02d", i);
        if (std::find(names.begin(), names.end(), candidate) == names.end()) return candidate;
    }
    return {};
}
