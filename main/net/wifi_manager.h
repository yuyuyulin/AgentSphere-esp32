#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "storage/settings_store.h"

namespace agentsphere {

class WifiManager {
public:
    esp_err_t Init(SettingsStore* store, QueueHandle_t event_queue);

    // Application-owned scan and fallback sequence. A new scan replaces the
    // remaining candidates. ESP_ERR_NOT_FOUND on the scan means no saved Wi-Fi.
    esp_err_t ConnectSavedNetworks();
    esp_err_t TryNextSavedNetwork();
    void ClearSavedCandidates() { saved_candidates_.clear(); }
    void CancelConnectionAttempt();
    int attempt() const { return attempt_.load(); }

    // Connects STA to the specified network (optionally persists to NVS upon success).
    esp_err_t ConnectTo(const std::string& ssid, const std::string& password, bool save_on_success = true);

    // Switches between APSTA (provisioning) and STA-only (normal operation).
    esp_err_t EnableSoftAp(const std::string& ap_ssid);
    esp_err_t DisableSoftAp();

    // Scans nearby 2.4GHz access points (deduplicated by strongest RSSI).
    std::vector<ScannedAp> ScanNearbyAps();

    bool is_connected() const { return connected_; }
    std::string current_ip() const;
    std::string GetSoftApSsid() const;
    std::string GetMacString() const;

private:
    static void OnWifiEvent(void* arg, esp_event_base_t base, int32_t event_id, void* data);
    static void OnIpEvent(void* arg, esp_event_base_t base, int32_t event_id, void* data);

    SettingsStore* store_ = nullptr;
    QueueHandle_t event_queue_ = nullptr;
    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;

    bool initialized_ = false;
    bool softap_active_ = false;
    std::atomic_bool connected_{false};
    std::atomic<int> attempt_{0};
    mutable std::mutex station_mutex_;
    bool connecting_ = false;
    bool save_pending_ = false;
    int retry_count_ = 0;

    std::string target_ssid_;
    std::string target_password_;
    std::vector<WifiCredential> saved_candidates_;
    std::string current_ip_;
};

}  // namespace agentsphere
