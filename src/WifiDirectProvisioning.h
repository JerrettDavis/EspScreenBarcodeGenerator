#pragma once

#include <cstdint>
#include <string>

namespace esplink {

struct WifiDirectCredentials {
    std::string ssid;
    std::string passphrase;
    uint16_t port = 0;
    bool valid = false;
};

// Persists the Wi-Fi Direct legacy-group join credentials a controller provisions over a
// trusted bootstrap transport (docs/PROTOCOL_V2.md §12.6 "Trusted Bluetooth bootstrap") in
// NVS, so the display can rejoin after a reboot without repeating the bootstrap step.
class WifiDirectProvisioning {
public:
    bool save(const WifiDirectCredentials& credentials, std::string& error);
    WifiDirectCredentials load() const;
    void clear();
};

}  // namespace esplink
