#include "WifiDirectProvisioning.h"

#include <Preferences.h>

namespace esplink {

namespace {
constexpr const char* kNamespace = "wifidirect";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPassphrase = "pass";
constexpr const char* kKeyPort = "port";
}  // namespace

bool WifiDirectProvisioning::save(const WifiDirectCredentials& credentials, std::string& error) {
    if (credentials.ssid.empty()) {
        error = "ssid must not be empty";
        return false;
    }
    if (credentials.port == 0) {
        error = "port must not be zero";
        return false;
    }

    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) {
        error = "failed to open NVS namespace";
        return false;
    }
    preferences.putString(kKeySsid, credentials.ssid.c_str());
    preferences.putString(kKeyPassphrase, credentials.passphrase.c_str());
    preferences.putUShort(kKeyPort, credentials.port);
    preferences.end();
    return true;
}

WifiDirectCredentials WifiDirectProvisioning::load() const {
    WifiDirectCredentials credentials;

    Preferences preferences;
    if (!preferences.begin(kNamespace, true)) return credentials;

    const String ssid = preferences.getString(kKeySsid, "");
    const String passphrase = preferences.getString(kKeyPassphrase, "");
    const uint16_t port = preferences.getUShort(kKeyPort, 0);
    preferences.end();

    if (ssid.length() == 0 || port == 0) return credentials;

    credentials.ssid = ssid.c_str();
    credentials.passphrase = passphrase.c_str();
    credentials.port = port;
    credentials.valid = true;
    return credentials;
}

void WifiDirectProvisioning::clear() {
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) return;
    preferences.clear();
    preferences.end();
}

}  // namespace esplink
