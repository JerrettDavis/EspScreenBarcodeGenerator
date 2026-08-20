#include <Arduino.h>

#include <string>

#include "BarcodeApplication.h"
#include "UsbProtocol.h"
#include "app_config.h"

namespace {
BarcodeApplication application;
UsbProtocol protocol(application);
}

void setup() {
    Serial.begin(app_config::kSerialBaud);
    Serial.setTimeout(25);
    delay(100);

    std::string error;
    if (!application.begin(error)) {
        Serial.printf("{\"event\":\"fatal\",\"message\":\"%s\"}\n", error.c_str());
        return;
    }
    protocol.begin();
}

void loop() {
    protocol.loop();
    application.loop();
    delay(1);
}
