#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "BarcodeApplicationAdapter.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "EspIdfDeviceControl.h"
#include "SerialLegacyEndpoint.h"
#include "app_config.h"

namespace {
BarcodeApplication application;
esplink::BarcodeApplicationAdapter applicationAdapter(application);
esplink::EspIdfDeviceControl deviceControl(application);
esplink::ControlProtocolEngine engine(applicationAdapter, applicationAdapter, deviceControl, ESPBARCODE_VERSION);
esplink::ControlSession legacySession(esplink::ControlSessionId{1}, esplink::ControllerId{1});
esplink::SerialLegacyEndpoint endpoint(engine, legacySession, deviceControl, applicationAdapter);
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
    endpoint.begin();
}

void loop() {
    endpoint.loop();
    application.loop();
    delay(1);
}
