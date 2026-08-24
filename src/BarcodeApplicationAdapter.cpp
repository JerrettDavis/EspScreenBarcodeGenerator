#include "BarcodeApplicationAdapter.h"

#include <utility>

namespace esplink {

bool BarcodeApplicationAdapter::generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) {
    application_.noteActivity();
    return application_.generate(spec, display, error);
}

bool BarcodeApplicationAdapter::setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                                                   espbarcode::Rotation rotation, bool invert,
                                                   const std::string& label, bool display, std::string& error) {
    application_.noteActivity();
    return application_.setUploadedMatrix(std::move(matrix), linear, quietZone, rotation, invert, label, display,
                                           error);
}

bool BarcodeApplicationAdapter::displayCurrent(std::string& error) {
    application_.noteActivity();
    return application_.displayCurrent(error);
}

void BarcodeApplicationAdapter::closeBarcode() {
    application_.noteActivity();
    application_.closeBarcode();
}

void BarcodeApplicationAdapter::showHome(const std::string& status) {
    application_.noteActivity();
    application_.showHome(status);
}

const espbarcode::BarcodeSpec& BarcodeApplicationAdapter::activeSpec() const {
    return application_.activeSpec();
}

const espbarcode::BarcodeResult& BarcodeApplicationAdapter::currentResult() const {
    return application_.currentResult();
}

bool BarcodeApplicationAdapter::hasCurrent() const {
    return application_.hasCurrent();
}

bool BarcodeApplicationAdapter::currentIsRaw() const {
    return application_.currentIsRaw();
}

uint8_t BarcodeApplicationAdapter::currentQuietZone() const {
    return application_.currentQuietZone();
}

espbarcode::Rotation BarcodeApplicationAdapter::currentRotation() const {
    return application_.currentRotation();
}

bool BarcodeApplicationAdapter::currentInvert() const {
    return application_.currentInvert();
}

const std::string& BarcodeApplicationAdapter::currentLabel() const {
    return application_.currentLabel();
}

bool BarcodeApplicationAdapter::barcodeVisible() const {
    return application_.barcodeVisible();
}

const std::string& BarcodeApplicationAdapter::statusText() const {
    return application_.statusText();
}

bool BarcodeApplicationAdapter::save(const std::string& name, const espbarcode::BarcodeSpec& spec,
                                      std::string& error) {
    application_.noteActivity();
    return application_.presets().save(name, spec, error);
}

bool BarcodeApplicationAdapter::load(const std::string& name, espbarcode::BarcodeSpec& spec,
                                      std::string& error) const {
    // `application_` is a reference member, so its constness is independent of this method's --
    // noteActivity() is fine to call here despite `load` itself being const.
    application_.noteActivity();
    return application_.presets().load(name, spec, error);
}

bool BarcodeApplicationAdapter::remove(const std::string& name, std::string& error) {
    application_.noteActivity();
    return application_.presets().remove(name, error);
}

std::vector<std::string> BarcodeApplicationAdapter::list() const {
    return application_.presets().list();
}

}  // namespace esplink
