#pragma once

#include <variant>
#include <vector>

#include "ProtocolCommands.h"

namespace esplink {

struct GoldenFixture {
    const char* name;
    Command command;
    std::variant<Response, ProtocolError> expected;
    bool encoderDependent = false;
    const char* precondition = "";  // human-readable, e.g. "requires a prior successful generate"
};

// Transcribed from docs/PROTOCOL.md and src/UsbProtocol.cpp (baseline commit
// d1565337b3d8f7b6cf8569cf0c443dfd43057072). Do not hand-tune these to make a
// later task's code pass — if a fixture looks wrong, the fixture is wrong and
// must be fixed against docs/PROTOCOL.md, not against the new implementation.
inline const std::vector<GoldenFixture>& goldenFixtures() {
    static const std::vector<GoldenFixture> fixtures = {
        // --- hello / capabilities / status (src/UsbProtocol.cpp:243-308) ---
        {"hello",
         HelloCommand{},
         HelloResponse{"EspScreenBarcodeGenerator", "1.0", /*firmware injected at build*/ "", "usb-uart-ndjson", 320, 480},
         false, ""},

        {"status_no_current",
         StatusCommand{},
         StatusResponse{/*barcodeVisible=*/false, /*hasCurrent=*/false, /*currentRaw=*/false,
                         /*status=*/"", /*freeHeap=*/0, std::nullopt},
         false, "fresh application, nothing generated yet; freeHeap is ignored by the replay test"},

        // --- generate (src/UsbProtocol.cpp:310-347) ---
        {"generate_qr_success",
         GenerateCommand{ /*spec*/ {}, /*display=*/true, std::nullopt},
         GenerateResponse{"qr", 0, 0, false, 0, true, "LAB-TEST-001"},
         /*encoderDependent=*/true,
         "spec.type=QrCode, spec.data=\"LAB-TEST-001\"; only type/displayed/normalizedData are checked literally"},

        {"generate_invalid_symbology",
         GenerateCommand{},  // Task 7's replay constructs this with an unparsable type sentinel
         ProtocolError{"generate", "invalid_spec", "unknown symbology"},
         false, "constructed by the replay test with a request JSON containing type=\"not-a-symbology\""},

        // --- display (src/UsbProtocol.cpp:349-368) ---
        {"display_no_current_fails",
         DisplayCommand{std::nullopt},
         ProtocolError{"display", "display_failed", ""},  // message text not asserted literally
         false, "fresh application, nothing generated yet"},

        // --- close / home (src/UsbProtocol.cpp:96-101) ---
        {"close",
         CloseCommand{},
         SimpleOkResponse{"close", "barcode closed"},
         false, ""},

        {"home",
         HomeCommand{},
         SimpleOkResponse{"home", "home screen displayed"},
         false, ""},

        // --- save / load / delete / list (src/UsbProtocol.cpp:370-430) ---
        {"save_missing_name",
         SaveCommand{""},
         ProtocolError{"save", "missing_name", "name is required"},
         false, ""},

        {"load_unknown_name",
         LoadCommand{"NO_SUCH_PRESET", false},
         ProtocolError{"load", "load_failed", ""},
         false, "empty PresetStore"},

        {"delete_missing_name",
         DeleteCommand{""},
         ProtocolError{"delete", "missing_name", "name is required"},
         false, ""},

        {"list_empty",
         ListCommand{},
         ListResponse{{}},
         false, "empty PresetStore"},

        // --- upload_begin validation (src/UsbProtocol.cpp:432-485) ---
        {"upload_begin_invalid_dimensions_zero_width",
         UploadBeginCommand{/*width=*/0, /*height=*/1, false, 4, espbarcode::Rotation::Auto, false, true, "x"},
         ProtocolError{"upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1"},
         false, ""},

        {"upload_begin_linear_height_must_be_one",
         UploadBeginCommand{/*width=*/9, /*height=*/2, /*linear=*/true, 4, espbarcode::Rotation::Auto, false, true, "x"},
         ProtocolError{"upload_begin", "invalid_dimensions", "matrix must be 1-512 modules; linear height must be 1"},
         false, ""},

        {"upload_begin_success_3x2",
         UploadBeginCommand{/*width=*/3, /*height=*/2, false, 4, espbarcode::Rotation::Auto, false, true, "external-pdf417"},
         UploadBeginResponse{/*bytesExpected=*/1, /*nextOffset=*/0},
         false, "(3*2+7)/8 = 1 byte"},

        // --- upload_chunk / upload_end (src/UsbProtocol.cpp:487-565) ---
        {"upload_chunk_wrong_offset",
         UploadChunkCommand{/*offset=*/1, {0xA8}},
         ProtocolError{"upload_chunk", "unexpected_offset", "chunks must be sequential"},
         false, "requires upload_begin_success_3x2 to have run first, nextOffset=0 expected"},

        {"upload_chunk_overflow",
         UploadChunkCommand{/*offset=*/0, {0xA8, 0x00}},
         ProtocolError{"upload_chunk", "overflow", "chunk exceeds declared matrix size"},
         false, "requires a 1-byte-declared upload in progress (see upload_begin_success_3x2)"},

        {"upload_end_incomplete",
         UploadEndCommand{std::nullopt},
         ProtocolError{"upload_end", "incomplete", "not all declared bytes were received"},
         false, "requires upload_begin_success_3x2 with zero chunks sent"},

        {"upload_end_crc_mismatch",
         UploadEndCommand{/*expectedCrc32=*/0},
         ProtocolError{"upload_end", "crc_mismatch", "uploaded bytes failed CRC32 validation"},
         false, "requires upload_begin_success_3x2 followed by upload_chunk{offset:0,data:[0xA8]}; actual crc32([0xA8]) = 168805463, not 0"},

        {"upload_end_success_3x2",
         UploadEndCommand{/*expectedCrc32=*/168805463u},
         UploadEndResponse{/*crc32=*/168805463u, /*displayed=*/true},
         false, "requires upload_begin_success_3x2 followed by upload_chunk{offset:0,data:[0xA8]}"},

        {"upload_abort",
         UploadAbortCommand{},
         SimpleOkResponse{"upload_abort", "upload discarded"},
         false, ""},

        // --- download (src/UsbProtocol.cpp:567-617) ---
        {"download_no_symbol",
         DownloadCommand{384},
         ProtocolError{"download", "no_symbol", "no current symbol"},
         false, "fresh application, nothing generated yet"},

        // --- backlight / reboot (src/UsbProtocol.cpp:121-129) ---
        {"backlight_on",
         BacklightCommand{true},
         SimpleOkResponse{"backlight", "backlight on"},
         false, ""},

        {"backlight_off",
         BacklightCommand{false},
         SimpleOkResponse{"backlight", "backlight off"},
         false, ""},

        {"reboot_acknowledged",
         RebootCommand{},
         SimpleOkResponse{"reboot", "rebooting"},
         false, "the actual ESP.restart() side effect is asserted separately via IDeviceControl, not through this fixture"},
    };
    return fixtures;
}

}  // namespace esplink
