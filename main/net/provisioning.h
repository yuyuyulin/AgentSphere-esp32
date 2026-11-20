#pragma once

#include <string>

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net/wifi_manager.h"
#include "storage/settings_store.h"

namespace agentsphere {

// On-demand SoftAP + Captive Portal DNS + HTTP provisioning server (192.168.4.1).
// Allocates resources only while active and releases them cleanly on Stop().
class ProvisioningServer {
public:
    esp_err_t Start(WifiManager* wifi, SettingsStore* store, QueueHandle_t event_queue);
    void Stop();
    bool is_running() const { return running_; }
    const std::string& ap_ssid() const { return ap_ssid_; }

private:
    static void DnsTask(void* arg);
    static esp_err_t HandleRoot(httpd_req_t* req);
    static esp_err_t HandleApiScan(httpd_req_t* req);
    static esp_err_t HandleApiSaved(httpd_req_t* req);
    static esp_err_t HandleApiConnect(httpd_req_t* req);
    static esp_err_t HandleApiForget(httpd_req_t* req);
    static esp_err_t HandleCaptiveRedirect(httpd_req_t* req, httpd_err_code_t err);

    WifiManager* wifi_ = nullptr;
    SettingsStore* store_ = nullptr;
    QueueHandle_t event_queue_ = nullptr;
    httpd_handle_t server_ = nullptr;
    TaskHandle_t dns_task_ = nullptr;
    int dns_sock_ = -1;
    volatile bool running_ = false;
    std::string ap_ssid_;
};

}  // namespace agentsphere
