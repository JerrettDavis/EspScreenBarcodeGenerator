#include <unity.h>

#include <cstdio>
#include <random>

#include "EspBarcodeCore.h"
#include "RandomPayload.h"
#include "UiRect.h"
#include "FakeTrustCrypto.h"
#include "TrustHandshake.h"
#include "TrustPairingSession.h"
#include "TrustStore.h"

using namespace espbarcode;
using uigeom::Rect;

void test_base64_round_trip() {
    const std::vector<uint8_t> bytes = {0, 1, 2, 253, 254, 255};
    std::vector<uint8_t> decoded;
    TEST_ASSERT_TRUE(bytesFromBase64(bytesToBase64(bytes), decoded));
    TEST_ASSERT_EQUAL_UINT32(bytes.size(), decoded.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), decoded.data(), bytes.size());
}

void test_retail_check_digits() {
    BarcodeSpec spec;
    spec.type = Symbology::Ean13;
    spec.data = "590123412345";
    auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_STRING("5901234123457", result.normalizedData.c_str());

    spec.type = Symbology::UpcA;
    spec.data = "03600029145";
    result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_STRING("036000291452", result.normalizedData.c_str());
}

void test_matrix_encoders() {
    BarcodeSpec spec;
    spec.type = Symbology::DataMatrix;
    spec.data = "PIO-DATAMATRIX";
    auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_UINT16(result.matrix.width(), result.matrix.height());

    spec.type = Symbology::Aztec;
    spec.data = "PIO-AZTEC";
    result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_EQUAL_UINT16(result.matrix.width(), result.matrix.height());
}

void test_qr_dependency_adapter() {
    BarcodeSpec spec;
    spec.type = Symbology::QrCode;
    spec.data = "PIO-QR-ENCODER";
    const auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    TEST_ASSERT_TRUE(result.matrix.width() >= 21);
    TEST_ASSERT_EQUAL_UINT16(result.matrix.width(), result.matrix.height());
    TEST_ASSERT_TRUE(result.matrix.get(0, 0));
    TEST_ASSERT_TRUE(result.matrix.get(6, 6));
}

void test_gs1_normalized_data_is_json_safe() {
    BarcodeSpec spec;
    spec.type = Symbology::Gs1_128;
    spec.data = "0109501101530003{FNC1}10ABC";
    const auto result = encode(spec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error.c_str());
    // ArduinoJson does not escape raw control bytes (e.g. the GS1 group
    // separator, 0x1D) when serializing, so normalizedData must never carry
    // one through into a field that gets echoed back in a JSON response.
    TEST_ASSERT_EQUAL_STRING("0109501101530003<GS>10ABC", result.normalizedData.c_str());
}

void test_pixel_exact_layout() {
    BitMatrix matrix(29, 29);
    const auto layout = calculateLayout(matrix, false, 320, 480, Rotation::Auto, 4, 2);
    TEST_ASSERT_TRUE_MESSAGE(layout.ok, layout.error.c_str());
    TEST_ASSERT_EQUAL_UINT16(8, layout.modulePixels);
    TEST_ASSERT_EQUAL_UINT16(0, static_cast<uint16_t>(layout.rotation));
}

void test_random_payload_always_encodes() {
    static constexpr Symbology kAllTypes[] = {
        Symbology::QrCode, Symbology::DataMatrix, Symbology::Aztec,
        Symbology::Code128, Symbology::Gs1_128, Symbology::Code39,
        Symbology::Ean13, Symbology::Ean8, Symbology::UpcA,
        Symbology::Itf, Symbology::Itf14, Symbology::Codabar, Symbology::Msi
    };

    std::mt19937 rng(1234567);
    const std::function<uint32_t()> nextRandom = [&rng] { return rng(); };

    for (Symbology type : kAllTypes) {
        for (int attempt = 0; attempt < 25; ++attempt) {
            BarcodeSpec spec;
            spec.type = type;
            spec.data = randomValidPayload(type, nextRandom);
            const auto result = encode(spec);
            char message[160];
            std::snprintf(message, sizeof(message), "%s payload '%s' failed: %s",
                          toString(type), spec.data.c_str(), result.error.c_str());
            TEST_ASSERT_TRUE_MESSAGE(result.ok, message);
        }
    }
}

void test_home_button_layout_has_no_overlaps_and_fits_screen() {
    // Mirrors the portrait (default 0/180-orientation) home-screen button
    // layout in BarcodeApplication.cpp. The OPTIONS/PRESETS/DISPLAY/RANDOM/
    // SETTINGS row is 5 evenly-distributed buttons (was 4, before per-screen
    // orientation added a Settings entry point).
    static constexpr Rect kButtons[] = {
        {8, 38, 150, 38},    // TYPE
        {166, 38, 70, 38},   // CLEAR
        {244, 38, 68, 38},   // SAVE
        {8, 176, 54, 38},    // OPTIONS
        {70, 176, 54, 38},   // PRESETS
        {132, 176, 54, 38},  // DISPLAY
        {194, 176, 54, 38},  // RANDOM
        {256, 176, 54, 38},  // SETTINGS
    };
    constexpr int16_t kScreenWidth = 320;
    constexpr std::size_t kCount = sizeof(kButtons) / sizeof(kButtons[0]);

    for (const Rect& r : kButtons) {
        TEST_ASSERT_TRUE(r.x >= 0);
        TEST_ASSERT_TRUE(r.x + r.w <= kScreenWidth);
    }
    for (std::size_t i = 0; i < kCount; ++i) {
        for (std::size_t j = i + 1; j < kCount; ++j) {
            TEST_ASSERT_FALSE_MESSAGE(uigeom::overlaps(kButtons[i], kButtons[j]), "home screen buttons overlap");
        }
    }
}

void test_landscape_home_button_layout_has_no_overlaps_and_fits_screen() {
    // Mirrors the landscape (90/270-orientation) home-screen layout, which
    // is now the default: 480x320 instead of 320x480.
    static constexpr Rect kButtons[] = {
        {8, 32, 260, 30},   // TYPE
        {276, 32, 96, 30},  // CLEAR
        {380, 32, 88, 30},  // SAVE
        {8, 130, 86, 32},   // OPTIONS
        {102, 130, 86, 32}, // PRESETS
        {196, 130, 86, 32}, // DISPLAY
        {290, 130, 86, 32}, // RANDOM
        {384, 130, 86, 32}, // SETTINGS
    };
    constexpr int16_t kScreenWidth = 480;
    constexpr int16_t kScreenHeight = 320;
    constexpr std::size_t kCount = sizeof(kButtons) / sizeof(kButtons[0]);

    for (const Rect& r : kButtons) {
        TEST_ASSERT_TRUE(r.x >= 0);
        TEST_ASSERT_TRUE(r.y >= 0);
        TEST_ASSERT_TRUE(r.x + r.w <= kScreenWidth);
        TEST_ASSERT_TRUE(r.y + r.h <= kScreenHeight);
    }
    for (std::size_t i = 0; i < kCount; ++i) {
        for (std::size_t j = i + 1; j < kCount; ++j) {
            TEST_ASSERT_FALSE_MESSAGE(uigeom::overlaps(kButtons[i], kButtons[j]),
                                      "landscape home screen buttons overlap");
        }
    }
}

void test_touch_pad_closes_gap_without_crossing_neighbor() {
    // Regression test for the CLEAR button being unreachable in the narrow
    // 8px gap between TYPE and CLEAR (and CLEAR and SAVE).
    constexpr Rect kType{8, 38, 150, 38};
    constexpr Rect kClear{166, 38, 70, 38};
    constexpr Rect kSave{244, 38, 68, 38};
    constexpr int16_t pad = 4;

    // A touch in what used to be dead space now resolves to CLEAR, not TYPE.
    TEST_ASSERT_FALSE(kType.contains(163, 57, pad));
    TEST_ASSERT_TRUE(kClear.contains(163, 57, pad));

    // Touches well inside TYPE never bleed into CLEAR.
    TEST_ASSERT_TRUE(kType.contains(100, 57, pad));
    TEST_ASSERT_FALSE(kClear.contains(100, 57, pad));

    // The CLEAR/SAVE gap splits cleanly with no double-match on either side.
    TEST_ASSERT_TRUE(kClear.contains(239, 57, pad));
    TEST_ASSERT_FALSE(kSave.contains(239, 57, pad));
    TEST_ASSERT_FALSE(kClear.contains(240, 57, pad));
    TEST_ASSERT_TRUE(kSave.contains(240, 57, pad));
}

void test_trust_hello_round_trip() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair aliceIdentity, bobIdentity;
    TEST_ASSERT_TRUE(crypto.generateKeyPair(aliceIdentity));
    TEST_ASSERT_TRUE(crypto.generateKeyPair(bobIdentity));

    esplink::TrustKeyPair aliceEphemeral, bobEphemeral;
    esplink::TrustNonce aliceNonce{}, bobNonce{};
    esplink::TrustHelloMessage aliceHello, bobHello;
    TEST_ASSERT_TRUE(esplink::buildTrustHello(crypto, aliceIdentity, aliceEphemeral, aliceNonce, aliceHello));
    TEST_ASSERT_TRUE(esplink::buildTrustHello(crypto, bobIdentity, bobEphemeral, bobNonce, bobHello));

    // Self-consistency (first-time pairing): each side accepts the other's own claimed key.
    TEST_ASSERT_TRUE(esplink::verifyTrustHello(crypto, aliceHello.staticPublicKey, aliceHello));
    TEST_ASSERT_TRUE(esplink::verifyTrustHello(crypto, bobHello.staticPublicKey, bobHello));

    // A tampered signature must not verify.
    esplink::TrustHelloMessage tampered = aliceHello;
    tampered.signature[0] ^= 0xFF;
    TEST_ASSERT_FALSE(esplink::verifyTrustHello(crypto, tampered.staticPublicKey, tampered));

    // Claiming a static key that doesn't match the embedded one is rejected outright.
    esplink::TrustHelloMessage spoofed = aliceHello;
    TEST_ASSERT_FALSE(esplink::verifyTrustHello(crypto, bobHello.staticPublicKey, spoofed));

    esplink::TrustDerivedKeys aliceDerived, bobDerived;
    TEST_ASSERT_TRUE(
        esplink::deriveFromHellos(crypto, aliceIdentity, aliceEphemeral, aliceNonce, bobHello, aliceDerived));
    TEST_ASSERT_TRUE(
        esplink::deriveFromHellos(crypto, bobIdentity, bobEphemeral, bobNonce, aliceHello, bobDerived));

    // Both sides must land on identical derived output despite opposite call order.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(aliceDerived.lmk.data(), bobDerived.lmk.data(), aliceDerived.lmk.size());
    TEST_ASSERT_EQUAL_UINT32(aliceDerived.numericCode, bobDerived.numericCode);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(aliceDerived.transcriptHash.data(), bobDerived.transcriptHash.data(),
                                 aliceDerived.transcriptHash.size());
}

void test_trust_store_add_find_forget() {
    esplink::TrustStore store;
    esplink::TrustRecord record;
    record.staticPublicKey.fill(0x11);
    record.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    record.peerRole = esplink::TrustRole::Client;

    TEST_ASSERT_TRUE(store.add(record));
    TEST_ASSERT_EQUAL_UINT32(1, store.size());

    const esplink::TrustRecord* byMac = store.findByMac(record.mac);
    TEST_ASSERT_NOT_NULL(byMac);
    TEST_ASSERT_EQUAL_UINT16(1, byMac->routeId);  // first assigned routeId is 1

    const esplink::TrustRecord* byKey = store.findByStaticKey(record.staticPublicKey);
    TEST_ASSERT_NOT_NULL(byKey);

    // Duplicate static key is rejected.
    esplink::TrustRecord duplicate = record;
    duplicate.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    TEST_ASSERT_FALSE(store.add(duplicate));
    TEST_ASSERT_EQUAL_UINT32(1, store.size());

    TEST_ASSERT_TRUE(store.forget(record.staticPublicKey));
    TEST_ASSERT_EQUAL_UINT32(0, store.size());
    TEST_ASSERT_NULL(store.findByMac(record.mac));
}

void test_trust_store_enforces_cap() {
    esplink::TrustStore store;
    for (std::size_t i = 0; i < esplink::TrustStore::kMaxRecords; ++i) {
        esplink::TrustRecord record;
        record.staticPublicKey.fill(static_cast<uint8_t>(i + 1));
        record.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, static_cast<uint8_t>(i)};
        TEST_ASSERT_TRUE(store.add(record));
    }
    TEST_ASSERT_TRUE(store.full());

    esplink::TrustRecord overflow;
    overflow.staticPublicKey.fill(0xFF);
    overflow.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_FALSE(store.add(overflow));
}

// Route ids must survive a reload from persistence: TrustConfigStore::loadRecords() parses each
// record's stored routeId and re-add()s it, and forget() is a swap-remove, so an add() that always
// reassigned would permute which peer a given routeId points at after any revocation.
void test_trust_store_preserves_loaded_route_ids() {
    esplink::TrustStore store;

    // Two records "reloaded from disk" with non-contiguous stored ids (what's left after the
    // record that held routeId 1 and 2 was forgotten in an earlier session).
    esplink::TrustRecord loadedThree;
    loadedThree.staticPublicKey.fill(0x33);
    loadedThree.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x33};
    loadedThree.routeId = 3;
    TEST_ASSERT_TRUE(store.add(loadedThree));
    TEST_ASSERT_EQUAL_UINT16(3, store.findByStaticKey(loadedThree.staticPublicKey)->routeId);

    esplink::TrustRecord loadedFive;
    loadedFive.staticPublicKey.fill(0x55);
    loadedFive.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x55};
    loadedFive.routeId = 5;
    TEST_ASSERT_TRUE(store.add(loadedFive));
    TEST_ASSERT_EQUAL_UINT16(5, store.findByStaticKey(loadedFive.staticPublicKey)->routeId);

    // A freshly-paired device (routeId still 0) takes the lowest free slot, skipping the loaded
    // ids rather than colliding with them.
    esplink::TrustRecord fresh;
    fresh.staticPublicKey.fill(0x77);
    fresh.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x77};
    TEST_ASSERT_TRUE(store.add(fresh));
    TEST_ASSERT_EQUAL_UINT16(1, store.findByStaticKey(fresh.staticPublicKey)->routeId);

    // A stored id that collides with one already in use is NOT honored -- duplicates would make
    // a routeId lookup ambiguous -- so it falls back to the lowest remaining free slot.
    esplink::TrustRecord collides;
    collides.staticPublicKey.fill(0x99);
    collides.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x99};
    collides.routeId = 3;  // already held by loadedThree
    TEST_ASSERT_TRUE(store.add(collides));
    TEST_ASSERT_EQUAL_UINT16(2, store.findByStaticKey(collides.staticPublicKey)->routeId);

    // An out-of-range stored id is likewise rejected in favour of a real free slot.
    esplink::TrustRecord outOfRange;
    outOfRange.staticPublicKey.fill(0xBB);
    outOfRange.mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xBB};
    outOfRange.routeId = 999;
    TEST_ASSERT_TRUE(store.add(outOfRange));
    TEST_ASSERT_EQUAL_UINT16(4, store.findByStaticKey(outOfRange.staticPublicKey)->routeId);

    // Every id handed out is still unique across the whole store.
    for (std::size_t i = 0; i < store.size(); ++i) {
        for (std::size_t j = i + 1; j < store.size(); ++j) {
            TEST_ASSERT_TRUE(store.at(i)->routeId != store.at(j)->routeId);
        }
    }
}

void test_trust_fingerprint_format() {
    esplink::TrustHash hash{};
    hash[0] = 0xA3;
    hash[1] = 0xF9;
    hash[2] = 0x21;
    hash[3] = 0xC4;
    TEST_ASSERT_EQUAL_STRING("A3F9-21C4", esplink::trustFingerprint(hash).c_str());
}

void test_trust_pairing_happy_path_both_confirm_first() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair aliceIdentity, bobIdentity;
    crypto.generateKeyPair(aliceIdentity);
    crypto.generateKeyPair(bobIdentity);

    esplink::TrustPairingSession alice(crypto);
    esplink::TrustPairingSession bob(crypto);

    esplink::TrustHelloMessage aliceHello;
    TEST_ASSERT_TRUE(alice.beginAsInitiator(aliceIdentity, 1000, aliceHello));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingPeerHello == alice.state());

    esplink::TrustHelloMessage bobHello;
    bool bobHasReply = false;
    TEST_ASSERT_TRUE(bob.onPeerHello(aliceHello, bobIdentity, 1001, bobHello, bobHasReply));
    TEST_ASSERT_TRUE(bobHasReply);
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingApproval == bob.state());

    esplink::TrustHelloMessage unusedReply;
    bool aliceHasReply = false;
    TEST_ASSERT_TRUE(alice.onPeerHello(bobHello, aliceIdentity, 1002, unusedReply, aliceHasReply));
    TEST_ASSERT_FALSE(aliceHasReply);
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingApproval == alice.state());

    esplink::TrustPairingOutcome aliceOutcome, bobOutcome;
    TEST_ASSERT_TRUE(alice.currentOutcome(aliceOutcome));
    TEST_ASSERT_TRUE(bob.currentOutcome(bobOutcome));
    TEST_ASSERT_EQUAL_UINT32(aliceOutcome.numericCode, bobOutcome.numericCode);  // same code both screens

    esplink::TrustSignature aliceConfirm, bobConfirm;
    TEST_ASSERT_TRUE(alice.confirmLocally(1003, aliceConfirm));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingPeerConfirm == alice.state());
    TEST_ASSERT_TRUE(bob.confirmLocally(1004, bobConfirm));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingPeerConfirm == bob.state());

    TEST_ASSERT_TRUE(alice.onPeerConfirm(bobConfirm, 1005));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Committed == alice.state());
    TEST_ASSERT_TRUE(bob.onPeerConfirm(aliceConfirm, 1006));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Committed == bob.state());

    TEST_ASSERT_EQUAL_UINT8_ARRAY(alice.derivedKeys().lmk.data(), bob.derivedKeys().lmk.data(),
                                 alice.derivedKeys().lmk.size());
}

void test_trust_pairing_peer_confirms_before_us() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair aliceIdentity, bobIdentity;
    crypto.generateKeyPair(aliceIdentity);
    crypto.generateKeyPair(bobIdentity);

    esplink::TrustPairingSession alice(crypto);
    esplink::TrustPairingSession bob(crypto);
    esplink::TrustHelloMessage aliceHello, bobHello, unused;
    bool hasReply = false;
    alice.beginAsInitiator(aliceIdentity, 0, aliceHello);
    bob.onPeerHello(aliceHello, bobIdentity, 0, bobHello, hasReply);
    alice.onPeerHello(bobHello, aliceIdentity, 0, unused, hasReply);

    esplink::TrustSignature bobConfirm;
    TEST_ASSERT_TRUE(bob.confirmLocally(0, bobConfirm));
    TEST_ASSERT_TRUE(alice.onPeerConfirm(bobConfirm, 0));
    // Alice hasn't tapped Confirm yet -- still waiting on the local human, not committed.
    TEST_ASSERT_TRUE(esplink::TrustPairingState::AwaitingApproval == alice.state());

    esplink::TrustSignature aliceConfirm;
    TEST_ASSERT_TRUE(alice.confirmLocally(0, aliceConfirm));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Committed == alice.state());  // peer already confirmed
}

void test_trust_pairing_cancel_and_timeout() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair identity;
    crypto.generateKeyPair(identity);

    esplink::TrustPairingSession session(crypto);
    esplink::TrustHelloMessage hello;
    session.beginAsInitiator(identity, 0, hello);

    bool shouldSend = false;
    session.cancel(shouldSend);
    TEST_ASSERT_TRUE(shouldSend);
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Cancelled == session.state());

    session.reset();
    session.beginAsInitiator(identity, 0, hello);
    TEST_ASSERT_FALSE(session.tick(60000));   // well under the 120s default pairing window
    TEST_ASSERT_FALSE(session.tick(119999));  // still inside it, right up to the boundary
    TEST_ASSERT_TRUE(session.tick(120001));   // past it
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Cancelled == session.state());
}

void test_trust_pairing_rejects_bad_signature() {
    esplink::FakeTrustCrypto crypto;
    esplink::TrustKeyPair identity;
    crypto.generateKeyPair(identity);

    esplink::TrustPairingSession responder(crypto);
    esplink::TrustHelloMessage forged;
    forged.staticPublicKey.fill(0xAA);
    forged.ephemeralPublicKey.fill(0xBB);
    forged.nonce.fill(0xCC);
    forged.signature.fill(0x00);  // does not match FakeTrustCrypto's deterministic MAC

    esplink::TrustHelloMessage reply;
    bool hasReply = false;
    TEST_ASSERT_FALSE(responder.onPeerHello(forged, identity, 0, reply, hasReply));
    TEST_ASSERT_TRUE(esplink::TrustPairingState::Idle == responder.state());  // reset(), not stuck Cancelled
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_base64_round_trip);
    RUN_TEST(test_retail_check_digits);
    RUN_TEST(test_matrix_encoders);
    RUN_TEST(test_qr_dependency_adapter);
    RUN_TEST(test_gs1_normalized_data_is_json_safe);
    RUN_TEST(test_pixel_exact_layout);
    RUN_TEST(test_random_payload_always_encodes);
    RUN_TEST(test_home_button_layout_has_no_overlaps_and_fits_screen);
    RUN_TEST(test_landscape_home_button_layout_has_no_overlaps_and_fits_screen);
    RUN_TEST(test_touch_pad_closes_gap_without_crossing_neighbor);
    RUN_TEST(test_trust_hello_round_trip);
    RUN_TEST(test_trust_store_add_find_forget);
    RUN_TEST(test_trust_store_enforces_cap);
    RUN_TEST(test_trust_store_preserves_loaded_route_ids);
    RUN_TEST(test_trust_fingerprint_format);
    RUN_TEST(test_trust_pairing_happy_path_both_confirm_first);
    RUN_TEST(test_trust_pairing_peer_confirms_before_us);
    RUN_TEST(test_trust_pairing_cancel_and_timeout);
    RUN_TEST(test_trust_pairing_rejects_bad_signature);
    return UNITY_END();
}
