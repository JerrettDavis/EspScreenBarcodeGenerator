#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ApplicationPorts.h"
#include "ControlProtocolEngine.h"
#include "ControlSession.h"
#include "Envelope.h"
#include "FrameAssembler.h"
#include "HopFrame.h"
#include "LengthPrefixFraming.h"
#include "WifiDirectProvisioning.h"

namespace esplink {

// Wi-Fi Direct legacy connector's ESP-side endpoint (docs/PROTOCOL_V2.md §12): the ESP joins
// the Windows-hosted legacy group as a plain Wi-Fi station, then opens a TCP client
// connection to the group owner and speaks EspLink v2 over the `CarrierProfileId::TcpStandard`
// carrier (uint32_le length prefix + raw hop frame, no COBS — see `LengthPrefixFraming`).
//
// Unlike `BleGattEndpoint`/`EspNowEndpoint`, this endpoint owns a connection-management state
// machine rather than a passive callback queue: nothing calls into it asynchronously, so
// `loop()` both drives Wi-Fi/TCP (re)connection and pumps the socket for inbound bytes.
//
// Credentials (SSID/passphrase/port) are never compiled in or entered on-device for this
// release — they arrive from `configure()`, called by a trusted bootstrap transport (BLE, in
// this codebase) once a Windows `WifiDirectLegacyConnector` has started its group. See
// docs/PROTOCOL_V2.md §12.6.
class WifiDirectTcpEndpoint : public IControlResponseSink {
public:
    WifiDirectTcpEndpoint(ControlProtocolEngine& engine, ControlSession& session, const IBarcodeDevice& device);

    // Loads any persisted credentials (from a prior `configure()` call) and, if present,
    // begins connecting. Safe to call with no persisted credentials: the endpoint just stays
    // idle until `configure()` is called.
    void begin();

    // Persists `ssid`/`passphrase`/`port` and (re)starts the connect sequence toward them,
    // tearing down any existing connection first. Returns false and leaves `error` set if the
    // credentials cannot be persisted.
    bool configure(const std::string& ssid, const std::string& passphrase, uint16_t port, std::string& error);

    // Drives the Wi-Fi-connect / TCP-connect / TCP-receive state machine. Call every loop().
    void loop();

    void send(const Response& response) override;
    void sendError(const ProtocolError& error) override;

    bool isConnected() const { return state_ == State::Connected; }

    static constexpr std::size_t kMaxFrameBytes = 4096;  // conservative vs. the 16 KiB protocol ceiling (§8.5)
    static constexpr uint32_t kWifiConnectTimeoutMs = 15000;
    static constexpr uint32_t kTcpConnectTimeoutMs = 8000;
    static constexpr uint32_t kReconnectBackoffMs = 5000;

private:
    enum class State { Idle, WifiConnecting, TcpConnecting, Connected };

    void processMessage(const MessageEnvelope& envelope, const std::vector<uint8_t>& body);
    void sendEnvelope(MessageKind kind, ServiceId serviceId, const std::vector<uint8_t>& bodyBytes,
                      uint64_t correlationId);
    void pumpReceive();
    void resetConnection(State nextState);

    static const char* mapV2Name(const std::string& name);

    ControlProtocolEngine& engine_;
    ControlSession& session_;
    const IBarcodeDevice& device_;
    FrameAssembler assembler_;
    WifiDirectProvisioning provisioning_;
    WifiDirectCredentials credentials_;
    LengthPrefixFrameParser frameParser_{kMaxFrameBytes};

    WiFiClient client_;
    State state_ = State::Idle;
    uint32_t stateEnteredAtMs_ = 0;
    uint32_t lastConnectAttemptMs_ = 0;

    uint32_t linkMessageCounter_ = 1;
    uint64_t nextResponseOperationId_ = 1;
    uint64_t currentRequestOperationId_ = 0;
    std::string currentRequestName_;
};

}  // namespace esplink
