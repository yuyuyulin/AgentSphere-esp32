#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "esp_err.h"

namespace agentsphere {

struct WifiCredential {
    std::string ssid;
    std::string password;
};

class SettingsStore {
public:
    static constexpr size_t kMaxSavedWifi = 5;

    esp_err_t Init();

    std::vector<WifiCredential> LoadWifiList() const;
    esp_err_t SaveWifi(const std::string& ssid, const std::string& password);
    esp_err_t RemoveWifi(const std::string& ssid);
    esp_err_t ClearWifiList();

    // Device credentials (Issue #3 bootstrap / pairing)
    bool LoadDeviceCredentials(std::string* out_uid, std::string* out_token) const;
    esp_err_t SaveDeviceCredentials(const std::string& uid, const std::string& token);
    esp_err_t ClearDeviceCredentials();

    // Pending pairing proof persisted across reboots until token is durably saved
    bool LoadPairingProof(std::string* out_pairing_id, std::string* out_claim_secret) const;
    esp_err_t SavePairingProof(const std::string& pairing_id, const std::string& claim_secret);
    esp_err_t ClearPairingProof();

    // Runtime-configurable AgentSphere HTTPS base URL (falls back to Kconfig default)
    std::string LoadServerBaseUrl() const;
    esp_err_t SaveServerBaseUrl(const std::string& url);

private:
    esp_err_t PersistWifiList(const std::vector<WifiCredential>& list) const;
};

}  // namespace agentsphere
