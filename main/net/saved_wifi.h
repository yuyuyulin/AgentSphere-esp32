#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace agentsphere {

struct WifiCredential {
    std::string ssid;
    std::string password;
};

struct ScannedAp {
    std::string ssid;
    int8_t rssi = -100;
    bool secured = true;
};

inline std::vector<WifiCredential> SavedWifiCandidates(const std::vector<WifiCredential>& saved,
                                                       const std::vector<ScannedAp>& scanned) {
    std::vector<WifiCredential> candidates;
    for (const auto& cred : saved) {
        const auto visible = [&cred](const ScannedAp& ap) { return ap.ssid == cred.ssid; };
        if (std::any_of(scanned.begin(), scanned.end(), visible)) candidates.push_back(cred);
    }
    // Retain support for a recently used hidden SSID when no saved AP was visible.
    if (candidates.empty() && !saved.empty()) candidates.push_back(saved.front());
    return candidates;
}

}  // namespace agentsphere
