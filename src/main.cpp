#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "BarcodeApplicationAdapter.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "EspIdfDeviceControl.h"
#include "EspNowEndpoint.h"
#include "SerialCobsEndpoint.h"
#include "SerialLegacyEndpoint.h"
#include "app_config.h"

namespace {
enum class ActiveTransport : uint8_t { Legacy, CobsV2 };

BarcodeApplication application;
esplink::BarcodeApplicationAdapter applicationAdapter(application);
esplink::EspIdfDeviceControl deviceControl(application);
esplink::ControlProtocolEngine engine(applicationAdapter, applicationAdapter, deviceControl, ESPBARCODE_VERSION);
esplink::ControlSession legacySession(esplink::ControlSessionId{1}, esplink::ControllerId{1});
esplink::ControlSession v2Session(esplink::ControlSessionId{2}, esplink::ControllerId{1});
esplink::ControlSession espNowSession(esplink::ControlSessionId{3}, esplink::ControllerId{1});
esplink::SerialLegacyEndpoint legacyEndpoint(engine, legacySession, deviceControl, applicationAdapter);
esplink::SerialCobsEndpoint cobsEndpoint(engine, v2Session, applicationAdapter);
esplink::EspNowEndpoint espNowEndpoint(engine, espNowSession, applicationAdapter);
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
    legacyEndpoint.begin();

    // ESP-NOW runs on its own radio, independent of whichever serial transport is active
    // above — a failure here is reported but not fatal to the rest of the device.
    std::string espNowError;
    if (espNowEndpoint.begin(espNowError)) {
        Serial.printf("{\"event\":\"espnow_ready\",\"mac\":\"%s\"}\n", espNowEndpoint.macAddress().c_str());
    } else {
        Serial.printf("{\"event\":\"espnow_error\",\"message\":\"%s\"}\n", espNowError.c_str());
    }
}

void loop() {
    if (active == ActiveTransport::Legacy) {
        legacyEndpoint.loop();
        if (legacyEndpoint.upgradeRequested()) active = ActiveTransport::CobsV2;
    } else {
        cobsEndpoint.loop();
    }
    espNowEndpoint.loop();
    application.loop();
    delay(1);
}
