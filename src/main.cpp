#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "BarcodeApplicationAdapter.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "EspIdfDeviceControl.h"
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
esplink::SerialLegacyEndpoint legacyEndpoint(engine, legacySession, deviceControl, applicationAdapter);
esplink::SerialCobsEndpoint cobsEndpoint(engine, v2Session, applicationAdapter);
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
}

void loop() {
    if (active == ActiveTransport::Legacy) {
        legacyEndpoint.loop();
        if (legacyEndpoint.upgradeRequested()) active = ActiveTransport::CobsV2;
    } else {
        cobsEndpoint.loop();
    }
    application.loop();
    delay(1);
}
