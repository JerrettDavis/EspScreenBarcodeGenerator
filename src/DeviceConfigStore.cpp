#include "DeviceConfigStore.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

using namespace esplink;

namespace {
constexpr const char* kConfigPath = "/config/device.json";
}

bool DeviceConfigStore::begin(std::string& error) {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        error = "LittleFS initialization failed";
        return false;
    }
    if (!LittleFS.exists("/config") && !LittleFS.mkdir("/config")) {
        error = "could not create config directory";
        return false;
    }
    // A missing or corrupt file just keeps the Deg90/Deg90 defaults; only a
    // write failure is a hard error.
    load();
    return true;
}

bool DeviceConfigStore::load() {
    File file = LittleFS.open(kConfigPath, "r");
    if (!file) return false;

    JsonDocument document;
    const DeserializationError parseError = deserializeJson(document, file);
    file.close();
    if (parseError) return false;

    ScreenOrientation barcode;
    ScreenOrientation editor;
    if (!tryParseScreenOrientation(document["barcode_orientation"] | 1, barcode)) return false;
    if (!tryParseScreenOrientation(document["editor_orientation"] | 1, editor)) return false;

    barcodeOrientation_ = barcode;
    editorOrientation_ = editor;
    return true;
}

bool DeviceConfigStore::save(std::string& error) const {
    JsonDocument document;
    document["schema"] = 1;
    document["barcode_orientation"] = static_cast<int>(barcodeOrientation_);
    document["editor_orientation"] = static_cast<int>(editorOrientation_);

    File file = LittleFS.open(kConfigPath, "w");
    if (!file) {
        error = "could not open device config for writing";
        return false;
    }
    const std::size_t written = serializeJson(document, file);
    file.close();
    if (written == 0) {
        error = "could not serialize device config";
        return false;
    }
    return true;
}

bool DeviceConfigStore::setOrientation(OrientationTarget target, ScreenOrientation value, std::string& error) {
    const ScreenOrientation previousBarcode = barcodeOrientation_;
    const ScreenOrientation previousEditor = editorOrientation_;
    if (target == OrientationTarget::Barcode) barcodeOrientation_ = value;
    else editorOrientation_ = value;

    if (!save(error)) {
        barcodeOrientation_ = previousBarcode;
        editorOrientation_ = previousEditor;
        return false;
    }
    return true;
}
