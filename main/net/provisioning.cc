#include "net/provisioning.h"

#include <cstring>
#include <string>

#include "app/events.h"
#include "boards/board.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace agentsphere {

namespace {

constexpr const char* kTag = "Provisioning";

// Compact, mobile-first provisioning page stored in Flash (zero heap copy)
constexpr const char kPortalHtml[] = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>小安 · AgentSphere Wi-Fi 配网</title>
<style>
  body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px}
  .card{max-width:440px;margin:0 auto 14px;background:#161b22;border:1px solid #30363d;border-radius:14px;padding:16px}
  h1{font-size:20px;margin:0 0 6px;color:#58f3c6}
  .sub{font-size:13px;color:#8b949e;margin:0 0 12px}
  .warn{background:#2d2208;border:1px solid #9e6a03;color:#f2cc60;border-radius:10px;padding:10px;font-size:12px;margin-bottom:12px}
  label{display:block;font-size:13px;color:#c9d1d9;margin:10px 0 4px}
  input{width:100%;box-sizing:border-box;padding:11px 12px;border-radius:10px;border:1px solid #30363d;background:#0d1117;color:#fff;font-size:15px}
  button{width:100%;padding:12px;border:0;border-radius:10px;background:#238636;color:#fff;font-size:15px;font-weight:600;margin-top:12px;cursor:pointer}
  button.sec{background:#21262d;border:1px solid #30363d;margin-top:8px}
  button.del{width:auto;padding:5px 10px;margin:0;background:#8b1e24;font-size:12px}
  .row{display:flex;justify-content:space-between;align-items:center;padding:9px 10px;border-radius:8px;background:#0d1117;margin-top:6px;cursor:pointer}
  .meta{font-size:12px;color:#8b949e}
  #msg{margin-top:10px;font-size:13px;color:#58f3c6;min-height:18px}
</style>
</head>
<body>
<div class="card">
  <h1>小安 · AgentSphere 配网</h1>
  <p class="sub" id="info">正在读取设备信息...</p>
  <div class="warn">提示：ESP32-S3 仅支持 <b>2.4GHz Wi-Fi</b>，请勿选择 5GHz 专用热点。</div>
  <label for="ssid">Wi-Fi 名称 (SSID)</label>
  <input id="ssid" placeholder="点击下方列表或手动输入 2.4GHz Wi-Fi">
  <label for="pass">Wi-Fi 密码</label>
  <input id="pass" type="password" placeholder="开放网络可留空">
  <button onclick="doConnect()">连接并保存</button>
  <div id="msg"></div>
</div>
<div class="card">
  <div style="display:flex;justify-content:space-between;align-items:center">
    <b>附近 2.4GHz Wi-Fi</b>
    <button class="sec" style="width:auto;padding:6px 12px;margin:0;font-size:12px" onclick="loadScan()">刷新</button>
  </div>
  <div id="scanList" class="meta" style="margin-top:8px">正在扫描...</div>
</div>
<div class="card">
  <b>已保存 Wi-Fi（最多 5 个）</b>
  <div id="savedList" class="meta" style="margin-top:8px">暂无</div>
</div>
<script>
async function loadSaved(){
  const r=await fetch('/api/saved'); const d=await r.json();
  document.getElementById('info').textContent=`${d.ap_ssid} · ${d.board} · v${d.fw_version} · ${d.mac}`;
  const el=document.getElementById('savedList');
  if(!d.saved||!d.saved.length){el.textContent='暂无已保存网络';return;}
  el.innerHTML='';
  d.saved.forEach(s=>{
    const row=document.createElement('div'); row.className='row';
    const name=document.createElement('span'); name.textContent=s;
    name.onclick=()=>{document.getElementById('ssid').value=s;};
    const btn=document.createElement('button'); btn.className='del'; btn.textContent='删除';
    btn.onclick=async(e)=>{e.stopPropagation();await fetch('/api/forget',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s})});loadSaved();};
    row.append(name,btn); el.append(row);
  });
}
async function loadScan(){
  const el=document.getElementById('scanList'); el.textContent='正在扫描...';
  const r=await fetch('/api/scan'); const list=await r.json();
  if(!list.length){el.textContent='未发现 2.4GHz 热点，请点击刷新';return;}
  el.innerHTML='';
  list.forEach(ap=>{
    const row=document.createElement('div'); row.className='row';
    row.onclick=()=>{document.getElementById('ssid').value=ap.ssid;document.getElementById('pass').focus();};
    const left=document.createElement('span'); left.textContent=ap.ssid;
    const right=document.createElement('span'); right.className='meta';
    right.textContent=`${ap.rssi} dBm ${ap.secured?'🔒':''}`;
    row.append(left,right); el.append(row);
  });
}
async function doConnect(){
  const ssid=document.getElementById('ssid').value.trim();
  const password=document.getElementById('pass').value;
  if(!ssid){document.getElementById('msg').textContent='请输入 Wi-Fi 名称';return;}
  document.getElementById('msg').textContent='正在发送配置并连接，请观察设备屏幕...';
  const r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password})});
  const d=await r.json();
  document.getElementById('msg').textContent=d.ok?'已提交！设备正在尝试联网，成功后会自动关闭配网热点。':('提交失败: '+(d.error||'未知错误'));
}
loadSaved(); loadScan();
</script>
</body>
</html>)HTML";

std::string ReadRequestBody(httpd_req_t* req) {
    if (req->content_len <= 0 || req->content_len > 512) {
        return {};
    }
    std::string body(req->content_len, '\0');
    int received = 0;
    while (received < req->content_len) {
        const int r = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (r <= 0) {
            return {};
        }
        received += r;
    }
    return body;
}

}  // namespace

esp_err_t ProvisioningServer::Start(WifiManager* wifi,
                                    SettingsStore* store,
                                    QueueHandle_t event_queue) {
    if (running_) return ESP_OK;
    wifi_ = wifi;
    store_ = store;
    event_queue_ = event_queue;
    ap_ssid_ = wifi_->GetSoftApSsid();

    ESP_ERROR_CHECK(wifi_->EnableSoftAp(ap_ssid_));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;

    esp_err_t err = httpd_start(&server_, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed: %s", esp_err_to_name(err));
        wifi_->DisableSoftAp();
        return err;
    }

    auto register_uri = [this](const char* uri, httpd_method_t method,
                               esp_err_t (*handler)(httpd_req_t*)) {
        httpd_uri_t u{};
        u.uri = uri;
        u.method = method;
        u.handler = handler;
        u.user_ctx = this;
        httpd_register_uri_handler(server_, &u);
    };

    register_uri("/", HTTP_GET, &ProvisioningServer::HandleRoot);
    register_uri("/api/scan", HTTP_GET, &ProvisioningServer::HandleApiScan);
    register_uri("/api/saved", HTTP_GET, &ProvisioningServer::HandleApiSaved);
    register_uri("/api/connect", HTTP_POST, &ProvisioningServer::HandleApiConnect);
    register_uri("/api/forget", HTTP_POST, &ProvisioningServer::HandleApiForget);
    httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND,
                               &ProvisioningServer::HandleCaptiveRedirect);

    running_ = true;
    xTaskCreatePinnedToCore(&ProvisioningServer::DnsTask, "captive_dns", 3072, this, 3,
                            &dns_task_, 0);

    ESP_LOGI(kTag, "Provisioning portal active at http://192.168.4.1 (SSID=%s)", ap_ssid_.c_str());
    return ESP_OK;
}

void ProvisioningServer::Stop() {
    if (!running_) return;
    running_ = false;

    if (dns_sock_ >= 0) {
        shutdown(dns_sock_, SHUT_RDWR);
        close(dns_sock_);
        dns_sock_ = -1;
    }
    if (dns_task_) {
        for (int i = 0; i < 20 && dns_task_ != nullptr; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }

    if (wifi_) {
        wifi_->DisableSoftAp();
    }

    ESP_LOGI(kTag, "Provisioning portal stopped (free internal heap=%u KB)",
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
}

void ProvisioningServer::DnsTask(void* arg) {
    auto* self = static_cast<ProvisioningServer*>(arg);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGW(kTag, "Failed to create DNS socket");
        self->dns_task_ = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    self->dns_sock_ = sock;

    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ESP_LOGW(kTag, "DNS bind(:53) failed");
        close(sock);
        self->dns_sock_ = -1;
        self->dns_task_ = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    uint8_t buf[256];
    while (self->running_) {
        sockaddr_in client{};
        socklen_t clen = sizeof(client);
        const int len = recvfrom(sock, buf, sizeof(buf) - 16, 0,
                                 reinterpret_cast<sockaddr*>(&client), &clen);
        if (len < 12) continue;

        // Build minimal DNS A-record response pointing to 192.168.4.1
        buf[2] = 0x81;  // Standard response, recursion desired
        buf[3] = 0x80;  // Recursion available, no error
        buf[6] = 0x00;
        buf[7] = 0x01;  // 1 Answer
        buf[8] = 0x00;
        buf[9] = 0x00;
        buf[10] = 0x00;
        buf[11] = 0x00;

        static constexpr uint8_t kAnswer[16] = {
            0xC0, 0x0C,              // Pointer to question name
            0x00, 0x01,              // Type A
            0x00, 0x01,              // Class IN
            0x00, 0x00, 0x00, 0x3C,  // TTL 60s
            0x00, 0x04,              // RDLENGTH 4
            192, 168, 4, 1           // 192.168.4.1
        };
        std::memcpy(buf + len, kAnswer, sizeof(kAnswer));
        sendto(sock, buf, len + sizeof(kAnswer), 0,
               reinterpret_cast<sockaddr*>(&client), clen);
    }

    if (self->dns_sock_ >= 0) {
        close(self->dns_sock_);
        self->dns_sock_ = -1;
    }
    self->dns_task_ = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t ProvisioningServer::HandleRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kPortalHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ProvisioningServer::HandleCaptiveRedirect(httpd_req_t* req, httpd_err_code_t /*err*/) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, "Redirecting to provisioning portal...", HTTPD_RESP_USE_STRLEN);
}

esp_err_t ProvisioningServer::HandleApiScan(httpd_req_t* req) {
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);
    const auto aps = self->wifi_->ScanNearbyAps();

    cJSON* arr = cJSON_CreateArray();
    for (const auto& ap : aps) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", ap.ssid.c_str());
        cJSON_AddNumberToObject(item, "rssi", ap.rssi);
        cJSON_AddBoolToObject(item, "secured", ap.secured);
        cJSON_AddItemToArray(arr, item);
    }
    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json ? json : "[]");
    cJSON_free(json);
    return err;
}

esp_err_t ProvisioningServer::HandleApiSaved(httpd_req_t* req) {
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);
    const auto saved = self->store_->LoadWifiList();
    const esp_app_desc_t* app = esp_app_get_description();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ap_ssid", self->ap_ssid_.c_str());
    cJSON_AddStringToObject(root, "mac", self->wifi_->GetMacString().c_str());
    cJSON_AddStringToObject(root, "board", Board::GetInstance().GetBoardName());
    cJSON_AddStringToObject(root, "fw_version", app ? app->version : "dev");
    cJSON_AddBoolToObject(root, "connected", self->wifi_->is_connected());

    cJSON* arr = cJSON_CreateArray();
    for (const auto& cred : saved) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(cred.ssid.c_str()));
    }
    cJSON_AddItemToObject(root, "saved", arr);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json ? json : "{}");
    cJSON_free(json);
    return err;
}

esp_err_t ProvisioningServer::HandleApiConnect(httpd_req_t* req) {
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);
    const std::string body = ReadRequestBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid_json\"}");
    }

    const cJSON* j_ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON* j_pass = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(j_ssid) || j_ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing_ssid\"}");
    }

    const std::string ssid = j_ssid->valuestring;
    const std::string pass = cJSON_IsString(j_pass) ? j_pass->valuestring : "";
    cJSON_Delete(root);

    const esp_err_t err = self->wifi_->ConnectTo(ssid, pass, true);
    if (err == ESP_OK && self->event_queue_) {
        AppEvent ev{};
        ev.type = AppEventType::ProvisioningConnectRequested;
        std::strncpy(ev.ssid, ssid.c_str(), sizeof(ev.ssid) - 1);
        xQueueSend(self->event_queue_, &ev, 0);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, err == ESP_OK ? "{\"ok\":true}"
                                                 : "{\"ok\":false,\"error\":\"connect_failed\"}");
}

esp_err_t ProvisioningServer::HandleApiForget(httpd_req_t* req) {
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);
    const std::string body = ReadRequestBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false}");
    }

    const cJSON* j_ssid = cJSON_GetObjectItem(root, "ssid");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(j_ssid)) {
        err = self->store_->RemoveWifi(j_ssid->valuestring);
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, err == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
}

}  // namespace agentsphere
