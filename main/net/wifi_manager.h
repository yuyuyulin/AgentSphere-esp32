#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "storage/settings_store.h"

namespace agentsphere {

struct ScannedAp {
    std::string ssid;
    int8_t rssi = -100;
    bool secured = true;
};

class WifiManager {
public:
    esp_err_t Init(SettingsStore* store, QueueHandle_t event_queue);

    // Attempts to connect to the highest-priority visible saved network.
    // Returns ESP_ERR_NOT_FOUND if no saved Wi-Fi exists in NVS.
    esp_err_t ConnectSavedNetworks();

    // Connects STA to the specified network (optionally persists to NVS upon success).
    esp_err_t ConnectTo(const std::string& ssid, const std::string& password, bool save_on_success = true);

    // Switches between APSTA (provisioning) and STA-only (normal operation).
    esp_err_t EnableSoftAp(const std::string& ap_ssid);
    esp_err_t DisableSoftAp();

    // Scans nearby 2.4GHz access points (deduplicated by strongest RSSI).
    std::vector<ScannedAp> ScanNearbyAps();

    bool is_connected() const { return connected_; }
    const std::string& current_ssid() const { return current_ssid_; }
    const std::string& current_ip() const { return current_ip_; }
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
    bool connected_ = false;
    bool save_pending_ = false;
    int retry_count_ = 0;

    std::string target_ssid_;
    std::string target_password_;
    std::string current_ssid_;
    std::string current_ip_;
};

}  // namespace agentsphere
