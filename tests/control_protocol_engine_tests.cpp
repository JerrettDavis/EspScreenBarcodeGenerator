#include "ApplicationPorts.h"
#include "ControlSession.h"

#include <iostream>
#include <map>

using namespace esplink;

namespace {
int failures = 0;
void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
}
}  // namespace
#define CHECK(expr) check((expr), #expr, __LINE__)

namespace {

class FakeBarcodeDevice : public IBarcodeDevice {
public:
    bool generate(const espbarcode::BarcodeSpec& spec, bool display, std::string& error) override {
        (void)error;
        spec_ = spec;
        current_ = espbarcode::encode(spec);
        hasCurrent_ = current_.ok;
        currentIsRaw_ = false;
        visible_ = display && hasCurrent_;
        if (!current_.ok) error = current_.error;
        return current_.ok;
    }
    bool setUploadedMatrix(espbarcode::BitMatrix&& matrix, bool linear, uint8_t quietZone,
                           espbarcode::Rotation rotation, bool invert, const std::string& label,
                           bool display, std::string& error) override {
        (void)error;
        current_ = espbarcode::BarcodeResult{};
        current_.ok = true;
        current_.matrix = std::move(matrix);
        current_.linear = linear;
        hasCurrent_ = true;
        currentIsRaw_ = true;
        quiet_ = quietZone;
        rotation_ = rotation;
        invert_ = invert;
        label_ = label;
        visible_ = display;
        return true;
    }
    bool displayCurrent(std::string& error) override {
        if (!hasCurrent_) { error = "no current symbol"; return false; }
        visible_ = true;
        return true;
    }
    void closeBarcode() override { visible_ = false; }
    void showHome(const std::string& status) override { visible_ = false; status_ = status; }

    const espbarcode::BarcodeSpec& activeSpec() const override { return spec_; }
    const espbarcode::BarcodeResult& currentResult() const override { return current_; }
    bool hasCurrent() const override { return hasCurrent_; }
    bool currentIsRaw() const override { return currentIsRaw_; }
    uint8_t currentQuietZone() const override { return quiet_; }
    espbarcode::Rotation currentRotation() const override { return rotation_; }
    bool currentInvert() const override { return invert_; }
    const std::string& currentLabel() const override { return label_; }
    bool barcodeVisible() const override { return visible_; }
    const std::string& statusText() const override { return status_; }

private:
    espbarcode::BarcodeSpec spec_;
    espbarcode::BarcodeResult current_;
    bool hasCurrent_ = false;
    bool currentIsRaw_ = false;
    uint8_t quiet_ = 4;
    espbarcode::Rotation rotation_ = espbarcode::Rotation::Auto;
    bool invert_ = false;
    std::string label_;
    bool visible_ = false;
    std::string status_;
};

class FakePresetRepository : public IPresetRepository {
public:
    bool save(const std::string& name, const espbarcode::BarcodeSpec& spec, std::string& error) override {
        (void)error;
        presets_[name] = spec;
        return true;
    }
    bool load(const std::string& name, espbarcode::BarcodeSpec& spec, std::string& error) const override {
        auto it = presets_.find(name);
        if (it == presets_.end()) { error = "preset not found"; return false; }
        spec = it->second;
        return true;
    }
    bool remove(const std::string& name, std::string& error) override {
        (void)error;
        return presets_.erase(name) > 0;
    }
    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto& [name, spec] : presets_) names.push_back(name);
        return names;
    }

private:
    std::map<std::string, espbarcode::BarcodeSpec> presets_;
};

class FakeDeviceControl : public IDeviceControl {
public:
    void setBacklight(bool on) override { backlightOn_ = on; }
    uint32_t freeHeapBytes() const override { return 123456; }
    void reboot() override { rebooted_ = true; }

    bool backlightOn() const { return backlightOn_; }
    bool rebooted() const { return rebooted_; }

private:
    bool backlightOn_ = true;
    bool rebooted_ = false;
};

void test_fake_barcode_device_generate_and_display() {
    FakeBarcodeDevice device;
    espbarcode::BarcodeSpec spec;
    spec.type = espbarcode::Symbology::Code128;
    spec.data = "PORT-TEST";
    std::string error;
    CHECK(device.generate(spec, true, error));
    CHECK(device.hasCurrent());
    CHECK(device.barcodeVisible());
    device.closeBarcode();
    CHECK(!device.barcodeVisible());
}

void test_fake_preset_repository_round_trip() {
    FakePresetRepository presets;
    espbarcode::BarcodeSpec spec;
    spec.data = "SAVED";
    std::string error;
    CHECK(presets.save("SLOT01", spec, error));
    espbarcode::BarcodeSpec loaded;
    CHECK(presets.load("SLOT01", loaded, error));
    CHECK(loaded.data == "SAVED");
    CHECK(!presets.load("NOPE", loaded, error));
}

void test_two_sessions_do_not_share_transfer_state() {
    ControlSession sessionA{ControlSessionId{1}, ControllerId{100}};
    ControlSession sessionB{ControlSessionId{2}, ControllerId{200}};

    sessionA.transfer().upload().active = true;
    sessionA.transfer().upload().bytes = {0xAA, 0xBB};
    sessionA.transfer().upload().nextOffset = 2;

    CHECK(!sessionB.transfer().upload().active);
    CHECK(sessionB.transfer().upload().bytes.empty());
    CHECK(sessionB.transfer().upload().nextOffset == 0);
}

void test_lease_cannot_be_acquired_twice_by_the_same_session() {
    ControlSession session{ControlSessionId{1}, ControllerId{100}};
    CHECK(session.tryAcquireLease());
    CHECK(!session.tryAcquireLease());
    session.releaseLease();
    CHECK(session.tryAcquireLease());
}

void test_duplicate_result_cache_round_trips_and_replays() {
    ControlSession session{ControlSessionId{1}, ControllerId{100}};
    CHECK(!session.lookupCachedResult(OperationId{42}).has_value());

    ControlSession::CommandResult result = Response{SimpleOkResponse{"save", "preset saved"}};
    session.cacheResult(OperationId{42}, result);

    auto cached = session.lookupCachedResult(OperationId{42});
    CHECK(cached.has_value());
    CHECK(std::holds_alternative<Response>(*cached));
    const auto& response = std::get<Response>(*cached);
    CHECK(std::holds_alternative<SimpleOkResponse>(response));
    CHECK(std::get<SimpleOkResponse>(response).message == "preset saved");
}

void test_duplicate_result_cache_evicts_oldest_when_full() {
    ControlSession session{ControlSessionId{1}, ControllerId{100}};
    for (uint64_t i = 0; i < 9; ++i) {
        ControlSession::CommandResult result = Response{SimpleOkResponse{"op", std::to_string(i)}};
        session.cacheResult(OperationId{i}, result);
    }
    // Capacity is 8; operation 0 should have been evicted by operation 8.
    CHECK(!session.lookupCachedResult(OperationId{0}).has_value());
    CHECK(session.lookupCachedResult(OperationId{8}).has_value());
}

}  // namespace

int main() {
    test_fake_barcode_device_generate_and_display();
    test_fake_preset_repository_round_trip();
    test_two_sessions_do_not_share_transfer_state();
    test_lease_cannot_be_acquired_twice_by_the_same_session();
    test_duplicate_result_cache_round_trips_and_replays();
    test_duplicate_result_cache_evicts_oldest_when_full();
    if (failures != 0) {
        std::cerr << failures << " control protocol engine test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All control protocol engine tests passed (fakes only so far)\n";
    return EXIT_SUCCESS;
}
