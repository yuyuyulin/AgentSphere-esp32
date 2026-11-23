#include "storage/settings_store.h"

#include <algorithm>
#include <cstdio>

#include "esp_log.h"
#include "nvs.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "SettingsStore";
constexpr const char* kNamespace = "as_cfg";
constexpr const char* kKeyWifiCount = "wifi_cnt";
constexpr const char* kKeyDevUid = "dev_uid";
constexpr const char* kKeyDevToken = "dev_tok";

bool ReadString(nvs_handle_t h, const char* key, std::string* out) {
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) {
        return false;
    }
    std::string buf(len, '\0');
    if (nvs_get_str(h, key, buf.data(), &len) != ESP_OK) {
        return false;
    }
    if (!buf.empty() && buf.back() == '\0') {
        buf.pop_back();
    }
    *out = std::move(buf);
    return true;
}
}  // namespace

esp_err_t SettingsStore::Init() {
    nvs_handle_t h = 0;
    const esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_close(h);
    }
    return err;
}

std::vector<WifiCredential> SettingsStore::LoadWifiList() const {
    std::vector<WifiCredential> list;
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
        return list;
    }

    uint8_t count = 0;
    if (nvs_get_u8(h, kKeyWifiCount, &count) != ESP_OK) {
        nvs_close(h);
        return list;
    }
    if (count > kMaxSavedWifi) {
        count = kMaxSavedWifi;
    }

    for (uint8_t i = 0; i < count; ++i) {
        char k_ssid[16];
        char k_pass[16];
        std::snprintf(k_ssid, sizeof(k_ssid), "wifi_s_%u", i);
        std::snprintf(k_pass, sizeof(k_pass), "wifi_p_%u", i);

        WifiCredential cred{};
        if (ReadString(h, k_ssid, &cred.ssid) && !cred.ssid.empty()) {
            ReadString(h, k_pass, &cred.password);
            list.push_back(std::move(cred));
        }
    }

    nvs_close(h);
    return list;
}

esp_err_t SettingsStore::PersistWifiList(const std::vector<WifiCredential>& list) const {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t count = static_cast<uint8_t>(std::min(list.size(), kMaxSavedWifi));
    for (uint8_t i = 0; i < kMaxSavedWifi; ++i) {
        char k_ssid[16];
        char k_pass[16];
        std::snprintf(k_ssid, sizeof(k_ssid), "wifi_s_%u", i);
        std::snprintf(k_pass, sizeof(k_pass), "wifi_p_%u", i);
        if (i < count) {
            nvs_set_str(h, k_ssid, list[i].ssid.c_str());
            nvs_set_str(h, k_pass, list[i].password.c_str());
        } else {
            nvs_erase_key(h, k_ssid);
            nvs_erase_key(h, k_pass);
        }
    }

    err = nvs_set_u8(h, kKeyWifiCount, count);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t SettingsStore::SaveWifi(const std::string& ssid, const std::string& password) {
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        return ESP_ERR_INVALID_ARG;
    }
    auto list = LoadWifiList();
    list.erase(
        std::remove_if(list.begin(), list.end(),
                       [&ssid](const WifiCredential& c) { return c.ssid == ssid; }),
        list.end());
    list.insert(list.begin(), WifiCredential{ssid, password});
    if (list.size() > kMaxSavedWifi) {
        list.resize(kMaxSavedWifi);
    }
    ESP_LOGI(kTag, "Saved Wi-Fi '%s' (total saved=%u)", ssid.c_str(),
             static_cast<unsigned>(list.size()));
    return PersistWifiList(list);
}

esp_err_t SettingsStore::RemoveWifi(const std::string& ssid) {
    auto list = LoadWifiList();
    const size_t before = list.size();
    list.erase(
        std::remove_if(list.begin(), list.end(),
                       [&ssid](const WifiCredential& c) { return c.ssid == ssid; }),
        list.end());
    if (list.size() == before) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(kTag, "Removed Wi-Fi '%s' (remaining=%u)", ssid.c_str(),
             static_cast<unsigned>(list.size()));
    return PersistWifiList(list);
}

esp_err_t SettingsStore::ClearWifiList() {
    return PersistWifiList({});
}

bool SettingsStore::LoadDeviceCredentials(std::string* out_uid, std::string* out_token) const {
    if (!out_uid || !out_token) return false;
    nvs_handle_t h = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    std::string uid, tok;
    const bool ok = ReadString(h, kKeyDevUid, &uid) && ReadString(h, kKeyDevToken, &tok) &&
                    !uid.empty() && !tok.empty();
    nvs_close(h);
    if (ok) {
        *out_uid = std::move(uid);
        *out_token = std::move(tok);
    }
    return ok;
}

esp_err_t SettingsStore::SaveDeviceCredentials(const std::string& uid, const std::string& token) {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, kKeyDevUid, uid.c_str());
    if (err == ESP_OK) err = nvs_set_str(h, kKeyDevToken, token.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t SettingsStore::ClearDeviceCredentials() {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, kKeyDevUid);
    nvs_erase_key(h, kKeyDevToken);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

}  // namespace agentsphere
