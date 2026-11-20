#include "net/wifi_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "app/events.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "WifiManager";
constexpr int kMaxDirectRetries = 3;
}  // namespace

esp_err_t WifiManager::Init(SettingsStore* store, QueueHandle_t event_queue) {
    if (initialized_) return ESP_OK;
    store_ = store;
    event_queue_ = event_queue;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    sta_netif_ = esp_netif_create_default_wifi_sta();
    ap_netif_ = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::OnWifiEvent, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::OnIpEvent, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    initialized_ = true;
    ESP_LOGI(kTag, "Initialized (MAC=%s, SoftAP=%s)",
             GetMacString().c_str(), GetSoftApSsid().c_str());
    return ESP_OK;
}

std::string WifiManager::GetMacString() const {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

std::string WifiManager::GetSoftApSsid() const {
    uint8_t mac[6]{};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "AgentSphere-%02X%02X", mac[4], mac[5]);
    return buf;
}

std::vector<ScannedAp> WifiManager::ScanNearbyAps() {
    std::vector<ScannedAp> result;
    if (!initialized_) return result;

    wifi_scan_config_t scan_cfg{};
    scan_cfg.show_hidden = false;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    const esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        return result;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count == 0) return result;
    if (count > 24) count = 24;

    std::vector<wifi_ap_record_t> records(count);
    if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
        return result;
    }

    std::map<std::string, ScannedAp> dedup;
    for (uint16_t i = 0; i < count; ++i) {
        const char* raw_ssid = reinterpret_cast<const char*>(records[i].ssid);
        if (raw_ssid[0] == '\0') continue;
        std::string ssid(raw_ssid);
        auto it = dedup.find(ssid);
        if (it == dedup.end() || records[i].rssi > it->second.rssi) {
            ScannedAp ap{};
            ap.ssid = ssid;
            ap.rssi = records[i].rssi;
            ap.secured = (records[i].authmode != WIFI_AUTH_OPEN);
            dedup[ssid] = std::move(ap);
        }
    }

    result.reserve(dedup.size());
    for (auto& kv : dedup) {
        result.push_back(std::move(kv.second));
    }
    std::sort(result.begin(), result.end(),
              [](const ScannedAp& a, const ScannedAp& b) { return a.rssi > b.rssi; });
    return result;
}

esp_err_t WifiManager::ConnectSavedNetworks() {
    if (!initialized_ || !store_) return ESP_ERR_INVALID_STATE;

    const auto saved = store_->LoadWifiList();
    if (saved.empty()) {
        ESP_LOGI(kTag, "No saved Wi-Fi networks found in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(kTag, "Found %u saved Wi-Fi network(s); scanning nearby 2.4GHz APs...",
             static_cast<unsigned>(saved.size()));
    const auto scanned = ScanNearbyAps();

    // Pick highest-priority saved SSID that is currently visible
    for (const auto& cred : saved) {
        for (const auto& ap : scanned) {
            if (ap.ssid == cred.ssid) {
                ESP_LOGI(kTag, "Matched visible saved Wi-Fi '%s' (rssi=%d)",
                         cred.ssid.c_str(), ap.rssi);
                return ConnectTo(cred.ssid, cred.password, true);
            }
        }
    }

    // Fallback to most-recently-used SSID (supports hidden SSIDs)
    ESP_LOGI(kTag, "No saved SSID matched scan; trying most recent '%s'", saved[0].ssid.c_str());
    return ConnectTo(saved[0].ssid, saved[0].password, true);
}

esp_err_t WifiManager::ConnectTo(const std::string& ssid,
                                 const std::string& password,
                                 bool save_on_success) {
    if (!initialized_ || ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        return ESP_ERR_INVALID_ARG;
    }

    connected_ = false;
    retry_count_ = 0;
    target_ssid_ = ssid;
    target_password_ = password;
    save_pending_ = save_on_success;

    wifi_config_t sta_cfg{};
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.ssid), ssid.c_str(), sizeof(sta_cfg.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.password), password.c_str(),
                 sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_LOGI(kTag, "Connecting STA to '%s'...", ssid.c_str());
    return esp_wifi_connect();
}

esp_err_t WifiManager::EnableSoftAp(const std::string& ap_ssid) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    wifi_config_t ap_cfg{};
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), ap_ssid.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = static_cast<uint8_t>(ap_ssid.size());
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    softap_active_ = true;
    ESP_LOGI(kTag, "SoftAP enabled: SSID='%s' IP=192.168.4.1", ap_ssid.c_str());
    return ESP_OK;
}

esp_err_t WifiManager::DisableSoftAp() {
    if (!initialized_ || !softap_active_) return ESP_OK;
    softap_active_ = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_LOGI(kTag, "SoftAP disabled; returned to STA-only mode");
    return ESP_OK;
}

void WifiManager::OnWifiEvent(void* arg,
                              esp_event_base_t /*base*/,
                              int32_t event_id,
                              void* /*data*/) {
    auto* self = static_cast<WifiManager*>(arg);
    if (!self) return;

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const bool was_connected = self->connected_;
        self->connected_ = false;
        self->current_ip_.clear();

        if (!self->target_ssid_.empty() && self->retry_count_ < kMaxDirectRetries) {
            ++self->retry_count_;
            ESP_LOGW(kTag, "STA disconnected; retry %d/%d for '%s'",
                     self->retry_count_, kMaxDirectRetries, self->target_ssid_.c_str());
            esp_wifi_connect();
        } else if (was_connected || self->retry_count_ >= kMaxDirectRetries) {
            AppEvent ev{};
            ev.type = was_connected ? AppEventType::WifiDisconnected : AppEventType::WifiConnectTimeout;
            if (self->event_queue_) {
                xQueueSend(self->event_queue_, &ev, 0);
            }
        }
    }
}

void WifiManager::OnIpEvent(void* arg,
                            esp_event_base_t /*base*/,
                            int32_t event_id,
                            void* data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (!self || event_id != IP_EVENT_STA_GOT_IP) return;

    auto* ip_ev = static_cast<ip_event_got_ip_t*>(data);
    char ip_buf[16];
    std::snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&ip_ev->ip_info.ip));

    self->connected_ = true;
    self->retry_count_ = 0;
    self->current_ssid_ = self->target_ssid_;
    self->current_ip_ = ip_buf;

    if (self->save_pending_ && self->store_ && !self->target_ssid_.empty()) {
        self->store_->SaveWifi(self->target_ssid_, self->target_password_);
        self->save_pending_ = false;
    }

    ESP_LOGI(kTag, "Connected to '%s' (IP=%s)", self->current_ssid_.c_str(), ip_buf);

    if (self->event_queue_) {
        AppEvent ev{};
        ev.type = AppEventType::WifiConnected;
        std::snprintf(ev.ssid, sizeof(ev.ssid), "%s", self->current_ssid_.c_str());
        std::snprintf(ev.ip, sizeof(ev.ip), "%s", ip_buf);
        xQueueSend(self->event_queue_, &ev, 0);
    }
}

}  // namespace agentsphere
