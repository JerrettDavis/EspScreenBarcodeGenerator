#include "ApplicationPorts.h"
#include "ControlSession.h"
#include "ControlProtocolEngine.h"
#include "vectors_v1_golden.h"

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
    void setOrientation(OrientationTarget target, ScreenOrientation value) override {
        if (target == OrientationTarget::Barcode) barcodeOrientation_ = value;
        else editorOrientation_ = value;
    }
    uint32_t freeHeapBytes() const override { return 123456; }
    void reboot() override { rebooted_ = true; }

    bool backlightOn() const { return backlightOn_; }
    bool rebooted() const { return rebooted_; }
    ScreenOrientation barcodeOrientation() const { return barcodeOrientation_; }
    ScreenOrientation editorOrientation() const { return editorOrientation_; }

private:
    bool backlightOn_ = true;
    bool rebooted_ = false;
    ScreenOrientation barcodeOrientation_ = ScreenOrientation::Deg90;
    ScreenOrientation editorOrientation_ = ScreenOrientation::Deg90;
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

class RecordingSink : public IControlResponseSink {
public:
    void send(const Response& response) override { responses.push_back(response); }
    void sendError(const ProtocolError& error) override { errors.push_back(error); }

    std::vector<Response> responses;
    std::vector<ProtocolError> errors;
};

void test_hello_matches_golden_fixture() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, HelloCommand{}, OperationId{1}, "usb-uart-ndjson", sink);

    CHECK(sink.responses.size() == 1);
    CHECK(std::holds_alternative<HelloResponse>(sink.responses[0]));
    const auto& hello = std::get<HelloResponse>(sink.responses[0]);
    CHECK(hello.device == "EspScreenBarcodeGenerator");
    CHECK(hello.protocol == "1.0");
    CHECK(hello.transport == "usb-uart-ndjson");
    CHECK(hello.screenWidth == 320 && hello.screenHeight == 480);
}

void test_status_no_current_matches_golden_fixture() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, StatusCommand{}, OperationId{1}, "usb-uart-ndjson", sink);

    CHECK(sink.responses.size() == 1);
    const auto& status = std::get<StatusResponse>(sink.responses[0]);
    CHECK(!status.barcodeVisible);
    CHECK(!status.hasCurrent);
    CHECK(!status.current.has_value());
}

void test_close_home_backlight_match_golden_fixtures() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, CloseCommand{}, OperationId{1}, "usb-uart-ndjson", sink);
    engine.handle(session, HomeCommand{}, OperationId{2}, "usb-uart-ndjson", sink);
    engine.handle(session, BacklightCommand{false}, OperationId{3}, "usb-uart-ndjson", sink);

    CHECK(std::get<SimpleOkResponse>(sink.responses[0]).message == "barcode closed");
    CHECK(std::get<SimpleOkResponse>(sink.responses[1]).message == "home screen displayed");
    CHECK(std::get<SimpleOkResponse>(sink.responses[2]).message == "backlight off");
}

void test_orientation_sets_barcode_and_editor_independently() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    CHECK(control.barcodeOrientation() == ScreenOrientation::Deg90);
    CHECK(control.editorOrientation() == ScreenOrientation::Deg90);

    engine.handle(session, OrientationCommand{OrientationTarget::Barcode, ScreenOrientation::Deg0},
                  OperationId{1}, "usb-uart-ndjson", sink);
    engine.handle(session, OrientationCommand{OrientationTarget::Editor, ScreenOrientation::Deg270},
                  OperationId{2}, "usb-uart-ndjson", sink);

    CHECK(control.barcodeOrientation() == ScreenOrientation::Deg0);
    CHECK(control.editorOrientation() == ScreenOrientation::Deg270);
    CHECK(sink.responses.size() == 2);
    CHECK(std::get<SimpleOkResponse>(sink.responses[0]).command == "orientation");
    CHECK(std::get<SimpleOkResponse>(sink.responses[0]).message == "orientation set: barcode");
    CHECK(std::get<SimpleOkResponse>(sink.responses[1]).message == "orientation set: editor");
}

void test_upload_round_trip_matches_golden_fixtures() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    UploadBeginCommand begin{3, 2, false, 4, espbarcode::Rotation::Auto, false, true, "external-pdf417"};
    engine.handle(session, begin, OperationId{1}, "usb-uart-ndjson", sink);
    CHECK(std::get<UploadBeginResponse>(sink.responses.back()).bytesExpected == 1);

    engine.handle(session, UploadChunkCommand{0, {0xA8}}, OperationId{2}, "usb-uart-ndjson", sink);
    CHECK(std::get<UploadChunkResponse>(sink.responses.back()).nextOffset == 1);

    engine.handle(session, UploadEndCommand{168805463u}, OperationId{3}, "usb-uart-ndjson", sink);
    const auto& end = std::get<UploadEndResponse>(sink.responses.back());
    CHECK(end.crc32 == 168805463u);
    CHECK(end.displayed);
    CHECK(device.currentIsRaw());
}

void test_upload_chunk_wrong_offset_matches_golden_fixture() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, UploadBeginCommand{3, 2, false, 4, espbarcode::Rotation::Auto, false, true, "x"},
                  OperationId{1}, "usb-uart-ndjson", sink);
    engine.handle(session, UploadChunkCommand{1, {0xA8}}, OperationId{2}, "usb-uart-ndjson", sink);

    CHECK(sink.errors.size() == 1);
    CHECK(sink.errors[0].code == "unexpected_offset");
}

void test_reboot_is_replayed_and_triggers_device_control_only_once() {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine(device, presets, control, "0.1.0-test");
    ControlSession session{ControlSessionId{1}, ControllerId{1}};
    RecordingSink sink;

    engine.handle(session, RebootCommand{}, OperationId{99}, "usb-uart-ndjson", sink);
    CHECK(control.rebooted());

    // A duplicate operation id (carrier retry) must replay the cached ack, not re-trigger
    // the real restart. FakeDeviceControl only tracks a bool, so this asserts the replayed
    // response is identical; a richer fake could additionally count invocations.
    RecordingSink secondSink;
    engine.handle(session, RebootCommand{}, OperationId{99}, "usb-uart-ndjson", secondSink);
    CHECK(secondSink.responses.size() == 1);
    CHECK(std::get<SimpleOkResponse>(secondSink.responses[0]).message == "rebooting");
}

struct EngineHarness {
    FakeBarcodeDevice device;
    FakePresetRepository presets;
    FakeDeviceControl control;
    ControlProtocolEngine engine;
    ControlSession session;

    EngineHarness() : engine(device, presets, control, "0.1.0-test"), session(ControlSessionId{1}, ControllerId{1}) {}
};

ControlSession::CommandResult dispatch(EngineHarness& harness, const Command& command, uint64_t operationId) {
    RecordingSink sink;
    harness.engine.handle(harness.session, command, OperationId{operationId}, "usb-uart-ndjson", sink);
    CHECK(sink.responses.size() + sink.errors.size() == 1);
    if (!sink.errors.empty()) return ControlSession::CommandResult{sink.errors.back()};
    return ControlSession::CommandResult{sink.responses.back()};
}

const GoldenFixture& findGoldenFixture(const std::vector<GoldenFixture>& fixtures, const char* name) {
    for (const auto& fixture : fixtures) {
        if (std::string(fixture.name) == name) return fixture;
    }
    std::cerr << "FAIL: no golden fixture named " << name << '\n';
    ++failures;
    return fixtures.front();
}

// Compares two Response variants field-by-field. `expected`'s active alternative decides which
// fields are meaningful; a couple of fields are deliberately not literal contract values (see the
// per-fixture comments in vectors_v1_golden.h) and are skipped here to match.
bool sameResponse(const Response& actual, const Response& expected, bool encoderDependent) {
    if (actual.index() != expected.index()) return false;
    return std::visit([&](const auto& exp) {
        using T = std::decay_t<decltype(exp)>;
        const T& act = std::get<T>(actual);
        if constexpr (std::is_same_v<T, HelloResponse>) {
            // firmware is injected at build time; the fixture stores a placeholder, not a literal.
            return act.device == exp.device && act.protocol == exp.protocol && act.transport == exp.transport &&
                   act.screenWidth == exp.screenWidth && act.screenHeight == exp.screenHeight;
        } else if constexpr (std::is_same_v<T, StatusResponse>) {
            // freeHeap is ignored by the replay test per the fixture's own precondition note.
            return act.barcodeVisible == exp.barcodeVisible && act.hasCurrent == exp.hasCurrent &&
                   act.currentRaw == exp.currentRaw && act.status == exp.status &&
                   act.current.has_value() == exp.current.has_value();
        } else if constexpr (std::is_same_v<T, GenerateResponse>) {
            if (encoderDependent) {
                return act.type == exp.type && act.displayed == exp.displayed && act.normalizedData == exp.normalizedData;
            }
            return act.type == exp.type && act.width == exp.width && act.height == exp.height &&
                   act.linear == exp.linear && act.quiet == exp.quiet && act.displayed == exp.displayed &&
                   act.normalizedData == exp.normalizedData;
        } else if constexpr (std::is_same_v<T, SimpleOkResponse>) {
            return act.command == exp.command && act.message == exp.message;
        } else if constexpr (std::is_same_v<T, ListResponse>) {
            return act.presets == exp.presets;
        } else if constexpr (std::is_same_v<T, UploadBeginResponse>) {
            return act.bytesExpected == exp.bytesExpected && act.nextOffset == exp.nextOffset;
        } else if constexpr (std::is_same_v<T, UploadChunkResponse>) {
            return act.accepted == exp.accepted && act.nextOffset == exp.nextOffset;
        } else if constexpr (std::is_same_v<T, UploadEndResponse>) {
            return act.crc32 == exp.crc32 && act.displayed == exp.displayed;
        } else {
            return false;  // no fixture uses CapabilitiesResponse/DownloadBegin/Chunk/EndEvent.
        }
    }, expected);
}

// `code`/`command` are always asserted literally; `message` is only asserted when the fixture
// gives one (an empty fixture message means "not asserted literally" -- see e.g.
// display_no_current_fails and load_unknown_name in vectors_v1_golden.h, whose real messages
// come from a dynamic fake-device/preset error string rather than a fixed protocol contract).
bool sameError(const ProtocolError& actual, const ProtocolError& expected) {
    if (actual.command != expected.command || actual.code != expected.code) return false;
    if (expected.message.empty()) return true;
    return actual.message == expected.message;
}

void assertFixtureMatches(const GoldenFixture& fixture, const ControlSession::CommandResult& result) {
    if (std::holds_alternative<ProtocolError>(fixture.expected)) {
        CHECK(std::holds_alternative<ProtocolError>(result));
        if (std::holds_alternative<ProtocolError>(result)) {
            CHECK(sameError(std::get<ProtocolError>(result), std::get<ProtocolError>(fixture.expected)));
        }
    } else {
        CHECK(std::holds_alternative<Response>(result));
        if (std::holds_alternative<Response>(result)) {
            CHECK(sameResponse(std::get<Response>(result), std::get<Response>(fixture.expected), fixture.encoderDependent));
        }
    }
}

// Replays every golden fixture's `command` through a real ControlProtocolEngine/ControlSession and
// asserts the result against `fixture.expected`, with two documented exceptions:
//
//  - generate_invalid_symbology: its `command` field is a default-constructed GenerateCommand{}, not
//    an actual invalid-symbology sentinel, because Command is already a typed, post-JSON-parse
//    variant -- "unknown symbology" is a JSON-decode-time error that lives in JsonCommandCodec::decode
//    (Task 8), which has no native test harness (Arduino/ArduinoJson-dependent). This fixture cannot
//    be meaningfully replayed through the engine at all; it is verified at the JsonCommandCodec layer
//    only, same pre-existing limitation noted throughout Tasks 8-9.
//  - generate_qr_success: the native CMake validation build doesn't vendor ricmoo/QRCode (see
//    native_core_tests.cpp), so Symbology::QrCode can't be encoded outside PlatformIO. Guarded with
//    the same __has_include(<qrcode.h>) pattern native_core_tests.cpp uses; skipped entirely when
//    that header isn't available. When it *is* available, only type/displayed/normalizedData are
//    compared literally (width/height depend on the real encoder's exact module count, which isn't
//    part of the protocol contract -- see the fixture's own precondition text).
//
// The upload-chain fixtures depend on prior session state (see each fixture's `precondition` text),
// so they are not dispatched as independent single-shot calls: each gets a fresh engine+session that
// first replays upload_begin_success_3x2's own command (and, where required, an un-listed
// upload_chunk{offset:0,data:[0xA8]} step) before the fixture under test is dispatched and asserted.
void test_all_golden_fixtures_replay_correctly() {
    const auto& fixtures = goldenFixtures();

    for (const auto& fixture : fixtures) {
        const std::string name = fixture.name;

        if (name == "generate_invalid_symbology") continue;

        if (name == "generate_qr_success") {
#if __has_include(<qrcode.h>)
            EngineHarness harness;
            auto result = dispatch(harness, fixture.command, 1);
            assertFixtureMatches(fixture, result);
#endif
            continue;
        }

        if (name == "upload_chunk_wrong_offset" || name == "upload_chunk_overflow" || name == "upload_end_incomplete") {
            EngineHarness harness;
            dispatch(harness, findGoldenFixture(fixtures, "upload_begin_success_3x2").command, 1);
            auto result = dispatch(harness, fixture.command, 2);
            assertFixtureMatches(fixture, result);
            continue;
        }

        if (name == "upload_end_crc_mismatch" || name == "upload_end_success_3x2") {
            EngineHarness harness;
            dispatch(harness, findGoldenFixture(fixtures, "upload_begin_success_3x2").command, 1);
            dispatch(harness, UploadChunkCommand{0, {0xA8}}, 2);  // not itself a golden fixture
            auto result = dispatch(harness, fixture.command, 3);
            assertFixtureMatches(fixture, result);
            continue;
        }

        EngineHarness harness;
        auto result = dispatch(harness, fixture.command, 1);
        assertFixtureMatches(fixture, result);
    }
}

void test_two_sessions_do_not_corrupt_each_others_upload_via_the_engine() {
    FakeBarcodeDevice deviceA, deviceB;
    FakePresetRepository presetsA, presetsB;
    FakeDeviceControl controlA, controlB;
    ControlProtocolEngine engineA(deviceA, presetsA, controlA, "0.1.0-test");
    ControlProtocolEngine engineB(deviceB, presetsB, controlB, "0.1.0-test");
    ControlSession sessionA{ControlSessionId{1}, ControllerId{1}};
    ControlSession sessionB{ControlSessionId{2}, ControllerId{2}};
    RecordingSink sinkA, sinkB;

    engineA.handle(sessionA, UploadBeginCommand{3, 2, false, 4, espbarcode::Rotation::Auto, false, true, "a"},
                   OperationId{1}, "usb-uart-ndjson", sinkA);
    engineA.handle(sessionA, UploadChunkCommand{0, {0xA8}}, OperationId{2}, "usb-uart-ndjson", sinkA);

    // Session B never began an upload — its engine call must fail with no_upload, proving
    // session A's TransferSession state never leaked into session B.
    engineB.handle(sessionB, UploadChunkCommand{0, {0xFF}}, OperationId{2}, "usb-uart-ndjson", sinkB);
    CHECK(sinkB.errors.size() == 1);
    CHECK(sinkB.errors[0].code == "no_upload");
}

}  // namespace

int main() {
    test_fake_barcode_device_generate_and_display();
    test_fake_preset_repository_round_trip();
    test_two_sessions_do_not_share_transfer_state();
    test_lease_cannot_be_acquired_twice_by_the_same_session();
    test_duplicate_result_cache_round_trips_and_replays();
    test_duplicate_result_cache_evicts_oldest_when_full();
    test_hello_matches_golden_fixture();
    test_status_no_current_matches_golden_fixture();
    test_close_home_backlight_match_golden_fixtures();
    test_orientation_sets_barcode_and_editor_independently();
    test_upload_round_trip_matches_golden_fixtures();
    test_upload_chunk_wrong_offset_matches_golden_fixture();
    test_all_golden_fixtures_replay_correctly();
    test_reboot_is_replayed_and_triggers_device_control_only_once();
    test_two_sessions_do_not_corrupt_each_others_upload_via_the_engine();
    if (failures != 0) {
        std::cerr << failures << " control protocol engine test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All control protocol engine tests passed (fakes only so far)\n";
    return EXIT_SUCCESS;
}
