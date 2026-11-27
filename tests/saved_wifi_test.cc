#include <cassert>

#include "net/saved_wifi.h"

using agentsphere::SavedWifiCandidates;
using agentsphere::ScannedAp;
using agentsphere::WifiCredential;

int main() {
    const std::vector<WifiCredential> saved = {{"last", "a"}, {"home", "b"}, {"hotspot", "c"}};
    auto candidates = SavedWifiCandidates(saved, {{"guest"}, {"hotspot"}, {"home"}});
    assert(candidates.size() == 2);
    assert(candidates[0].ssid == "home" && candidates[0].password == "b");
    assert(candidates[1].ssid == "hotspot" && candidates[1].password == "c");

    candidates = SavedWifiCandidates(saved, {{"home", -40}, {"last", -85}});
    assert(candidates.size() == 2 && candidates[0].ssid == "last" && candidates[1].ssid == "home");

    candidates = SavedWifiCandidates(saved, {{"guest"}});
    assert(candidates.size() == 1 && candidates[0].ssid == "last");
    assert(SavedWifiCandidates({}, {{"guest"}}).empty());
}
