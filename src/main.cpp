#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "BarcodeApplicationAdapter.h"
#include "BleGattEndpoint.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "EspIdfDeviceControl.h"
#include "EspNowEndpoint.h"
#include "GatewayRelay.h"
#include "SerialCobsEndpoint.h"
#include "SerialLegacyEndpoint.h"
#include "TrustCrypto.h"
#include "WifiDirectTcpEndpoint.h"
#include "app_config.h"

namespace {
// GatewayRelay is a one-way switch, same as Legacy->CobsV2: once a host requests it (over the
// legacy USB line, see SerialLegacyEndpoint's "gateway" command), this board stops dispatching
// USB commands locally and becomes a pure USB<->ESP-NOW relay for the rest of the boot session.
enum class ActiveTransport : uint8_t { Legacy, CobsV2, GatewayRelayMode };

BarcodeApplication application;
esplink::BarcodeApplicationAdapter applicationAdapter(application);
esplink::EspIdfDeviceControl deviceControl(application);
esplink::ControlProtocolEngine engine(applicationAdapter, applicationAdapter, deviceControl, ESPBARCODE_VERSION);
esplink::ControlSession legacySession(esplink::ControlSessionId{1}, esplink::ControllerId{1});
esplink::ControlSession v2Session(esplink::ControlSessionId{2}, esplink::ControllerId{1});
esplink::ControlSession espNowSession(esplink::ControlSessionId{3}, esplink::ControllerId{1});
esplink::ControlSession bleSession(esplink::ControlSessionId{4}, esplink::ControllerId{1});
esplink::ControlSession wifiDirectSession(esplink::ControlSessionId{5}, esplink::ControllerId{1});
esplink::SerialLegacyEndpoint legacyEndpoint(engine, legacySession, deviceControl, applicationAdapter);
esplink::SerialCobsEndpoint cobsEndpoint(engine, v2Session, applicationAdapter);
esplink::EspNowEndpoint espNowEndpoint(engine, espNowSession, applicationAdapter);
esplink::WifiDirectTcpEndpoint wifiDirectEndpoint(engine, wifiDirectSession, applicationAdapter);
// BLE is this build's trusted bootstrap transport for Wi-Fi Direct credential provisioning
// (docs/PROTOCOL_V2.md §12.6) — it forwards `device.wifiDirect.configure` to wifiDirectEndpoint.
esplink::BleGattEndpoint bleEndpoint(engine, bleSession, applicationAdapter, &wifiDirectEndpoint);
esplink::GatewayRelay gatewayRelay;
ActiveTransport active = ActiveTransport::Legacy;
}  // namespace

void setup() {
    Serial.begin(app_config::kSerialBaud);
    Serial.setTimeout(25);
    delay(100);

    std::string error;
    if (!application.begin(error)) {
        Serial.printf("{\"event\":\"fatal\",\"message\":\"%s\"}\n", error.c_str());
        return;
    }

    // One-time boot self-test for the mbedTLS-backed trust crypto module (Task 4): proves
    // keygen/sign/verify/ECDH/HKDF actually work on this build before anything (Tasks 6-8)
    // depends on it, so a broken mbedTLS integration shows up in the serial log immediately
    // instead of silently failing on the first real pairing attempt.
    esplink::TrustCrypto trustCrypto;
    std::string trustCryptoError;
    if (!trustCrypto.begin(trustCryptoError) || !trustCrypto.selfTest(trustCryptoError)) {
        Serial.printf("{\"event\":\"trust_crypto_selftest_failed\",\"message\":\"%s\"}\n", trustCryptoError.c_str());
    } else {
        Serial.printf("{\"event\":\"trust_crypto_selftest_passed\"}\n");
    }

    legacyEndpoint.begin();

    // ESP-NOW and BLE each run on their own radio/stack, independent of whichever serial
    // transport is active above — a failure in either is reported but not fatal to the
    // rest of the device.
    std::string espNowError;
    if (espNowEndpoint.begin(espNowError)) {
        Serial.printf("{\"event\":\"espnow_ready\",\"mac\":\"%s\"}\n", espNowEndpoint.macAddress().c_str());
    } else {
        Serial.printf("{\"event\":\"espnow_error\",\"message\":\"%s\"}\n", espNowError.c_str());
    }
    engine.setGatewayLinkStatusSource(&espNowEndpoint);

    std::string bleError;
    if (bleEndpoint.begin("EspScreenBarcodeGenerator", bleError)) {
        Serial.printf("{\"event\":\"ble_ready\"}\n");
    } else {
        Serial.printf("{\"event\":\"ble_error\",\"message\":\"%s\"}\n", bleError.c_str());
    }

    // No-op until a controller provisions Wi-Fi Direct credentials over BLE (or a prior
    // provisioning was persisted to NVS) — see WifiDirectTcpEndpoint::begin().
    wifiDirectEndpoint.begin();
}

void loop() {
    if (active == ActiveTransport::Legacy) {
        legacyEndpoint.loop();
        if (legacyEndpoint.gatewayRequested()) {
            active = ActiveTransport::GatewayRelayMode;
            gatewayRelay.begin(espNowEndpoint.macAddress());  // takes over the ESP-NOW recv callback
            application.enterGatewayMode();
        } else if (legacyEndpoint.upgradeRequested()) {
            active = ActiveTransport::CobsV2;
        }
    } else if (active == ActiveTransport::CobsV2) {
        cobsEndpoint.loop();
    } else {
        gatewayRelay.loop();
        if (application.consumeGatewayPingRequest()) gatewayRelay.pingNow();
        application.updateGatewayStats(gatewayRelay.stats());
    }
    // GatewayRelayMode owns the ESP-NOW receive callback itself (see above); espNowEndpoint's
    // own loop() would just drain an empty queue, but skip it anyway for clarity.
    if (active != ActiveTransport::GatewayRelayMode) {
        espNowEndpoint.loop();
        application.updateGatewayLinkStatus(espNowEndpoint.gatewayLinkStatus());
    }
    bleEndpoint.loop();
    wifiDirectEndpoint.loop();
    application.loop();
    delay(1);
}
