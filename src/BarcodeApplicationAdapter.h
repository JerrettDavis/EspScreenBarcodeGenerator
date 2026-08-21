#pragma once

#include "ApplicationPorts.h"
#include "BarcodeApplication.h"

namespace esplink {

class BarcodeApplicationAdapter : public IBarcodeDevice, public IPresetRepository {
public:
    explicit BarcodeApplicationAdapter(BarcodeApplication& application) : application_(application) {}

    bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) override;
    bool setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                           espbarcode::Rotation rotation, bool invert, const std::string& label,
                           bool display, std::string& error) override;
    bool displayCurrent(std::string& error) override;
    void closeBarcode() override;
    void showHome(const std::string& status) override;

    const espbarcode::BarcodeSpec& activeSpec() const override;
    const espbarcode::BarcodeResult& currentResult() const override;
    bool hasCurrent() const override;
    bool currentIsRaw() const override;
    uint8_t currentQuietZone() const override;
    espbarcode::Rotation currentRotation() const override;
    bool currentInvert() const override;
    const std::string& currentLabel() const override;
    bool barcodeVisible() const override;
    const std::string& statusText() const override;

    bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error) override;
    bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const override;
    bool remove(const std::string& name, std::string& error) override;
    std::vector<std::string> list() const override;

private:
    BarcodeApplication& application_;
};

}  // namespace esplink
