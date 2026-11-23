#include "protocol/bootstrap_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "app/events.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"

namespace agentsphere {

namespace {

constexpr const char* kTag = "Bootstrap";
constexpr int kHttpTimeoutMs = 10000;
constexpr int kDefaultPollSec = 3;

struct HttpResponseBuffer {
    std::string body;
    int retry_after_sec = 0;
};

esp_err_t HttpEventHandler(esp_http_client_event_t* evt) {
    auto* ctx = static_cast<HttpResponseBuffer*>(evt->user_data);
    if (!ctx) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value) {
        if (strcasecmp(evt->header_key, "Retry-After") == 0) {
            const int v = std::atoi(evt->header_value);
            if (v > 0 && v < 600) {
                ctx->retry_after_sec = v;
            }
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        if (ctx->body.size() + static_cast<size_t>(evt->data_len) <= 4096) {
            ctx->body.append(static_cast<const char*>(evt->data),
                             static_cast<size_t>(evt->data_len));
        }
    }
    return ESP_OK;
}

std::string SanitizeFwVersion() {
    const esp_app_desc_t* app = esp_app_get_description();
    if (!app || app->version[0] == '\0') {
        return "0.1.0";
    }
    std::string out;
    out.reserve(32);
    for (const char* p = app->version; *p != '\0' && out.size() < 64; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-';
        if (ok) {
            out.push_back(c);
        }
    }
    return out.empty() ? "0.1.0" : out;
}

std::string ExtractErrorCode(cJSON* root) {
    if (!root) return {};
    const cJSON* j_code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsString(j_code) && j_code->valuestring) {
        return j_code->valuestring;
    }
    const cJSON* j_err = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsObject(j_err)) {
        const cJSON* nested = cJSON_GetObjectItem(j_err, "code");
        if (cJSON_IsString(nested) && nested->valuestring) {
            return nested->valuestring;
        }
    }
    return {};
}

}  // namespace

std::string BootstrapClient::GenerateClaimSecretHex() {
    uint8_t bytes[32]{};
    esp_fill_random(bytes, sizeof(bytes));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        out[i * 2] = kHex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[bytes[i] & 0x0F];
    }
    return out;
}

void BootstrapClient::Init(SettingsStore* store, WifiManager* wifi, QueueHandle_t event_queue) {
    store_ = store;
    wifi_ = wifi;
    event_queue_ = event_queue;
}

void BootstrapClient::Start() {
    if (running_ && task_ != nullptr) {
        refresh_requested_ = true;
        return;
    }
    running_ = true;
    refresh_requested_ = false;
    xTaskCreatePinnedToCore(&BootstrapClient::TaskEntry, "bootstrap_cli", 8192, this, 4,
                            &task_, 0);
}

void BootstrapClient::RequestRefresh() {
    refresh_requested_ = true;
    if (!running_ && task_ == nullptr) {
        Start();
    }
}

void BootstrapClient::Stop() {
    running_ = false;
    for (int i = 0; i < 40 && task_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void BootstrapClient::TaskEntry(void* arg) {
    auto* self = static_cast<BootstrapClient*>(arg);
    self->RunLoop();
    self->task_ = nullptr;
    vTaskDelete(nullptr);
}

void BootstrapClient::SyncRtcFromServerMs(int64_t server_time_ms) {
    if (server_time_ms < 1700000000000LL) return;

    const time_t sec = static_cast<time_t>(server_time_ms / 1000LL);
    struct timeval tv{};
    tv.tv_sec = sec;
    tv.tv_usec = static_cast<suseconds_t>((server_time_ms % 1000LL) * 1000LL);
    settimeofday(&tv, nullptr);

    if (auto* rtc = Board::GetInstance().GetRtcClock()) {
        struct tm tm_info{};
        gmtime_r(&sec, &tm_info);
        RtcTime rt{};
        rt.year = tm_info.tm_year + 1900;
        rt.month = tm_info.tm_mon + 1;
        rt.day = tm_info.tm_mday;
        rt.hour = tm_info.tm_hour;
        rt.minute = tm_info.tm_min;
        rt.second = tm_info.tm_sec;
        rt.valid = true;
        rtc->SetTime(rt);
    }
    ESP_LOGI(kTag, "Synced RTC from server_time_ms=%lld", static_cast<long long>(server_time_ms));
}

void BootstrapClient::EmitBoundEvent(const char* detail) {
    if (!event_queue_) return;
    AppEvent ev{};
    ev.type = AppEventType::BootstrapBound;
    std::snprintf(ev.text, sizeof(ev.text), "%s", detail ? detail : "Bound");
    xQueueSend(event_queue_, &ev, 0);
}

void BootstrapClient::EmitPairingEvent(const char* code, const char* hint, int expires_in) {
    if (!event_queue_) return;
    AppEvent ev{};
    ev.type = AppEventType::BootstrapPairingCode;
    if (code) {
        std::snprintf(ev.pairing_code, sizeof(ev.pairing_code), "%s", code);
    }
    std::snprintf(ev.text, sizeof(ev.text), "%s", hint ? hint : "");
    ev.value = expires_in;
    xQueueSend(event_queue_, &ev, 0);
}

void BootstrapClient::EmitErrorEvent(const char* message) {
    if (!event_queue_) return;
    AppEvent ev{};
    ev.type = AppEventType::BootstrapError;
    std::snprintf(ev.text, sizeof(ev.text), "%s", message ? message : "Bootstrap error");
    xQueueSend(event_queue_, &ev, 0);
}

BootstrapClient::HttpOutcome BootstrapClient::CallBootstrap(
    const std::string& server_url,
    const std::string& device_token,
    const std::string& claim_secret,
    std::string* out_pairing_id,
    std::string* out_pairing_code,
    int* out_expires_in,
    int* out_poll_sec,
    int* out_retry_after,
    bool* out_is_bound) {
    const std::string url = server_url + "/device/v1/bootstrap";
    Board& board = Board::GetInstance();
    const BoardCapabilities caps = board.GetCapabilities();

    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "hardware_id", wifi_->GetMacString().c_str());
    cJSON_AddStringToObject(req, "board", board.GetBoardName());
    cJSON_AddStringToObject(req, "fw_version", SanitizeFwVersion().c_str());
    cJSON_AddNumberToObject(req, "protocol_version", 1);
    if (device_token.empty() && !claim_secret.empty()) {
        cJSON_AddStringToObject(req, "claim_secret", claim_secret.c_str());
    }

    cJSON* j_caps = cJSON_CreateObject();
    cJSON_AddBoolToObject(j_caps, "has_screen", caps.has_screen);
    if (caps.has_screen && caps.screen_desc) {
        cJSON_AddStringToObject(j_caps, "screen", caps.screen_desc);
    }
    cJSON* j_fmts = cJSON_CreateArray();
    cJSON_AddItemToArray(j_fmts, cJSON_CreateString("opus"));
    cJSON_AddItemToObject(j_caps, "audio_formats", j_fmts);
    cJSON_AddBoolToObject(j_caps, "aec", caps.has_aec);
    cJSON_AddItemToObject(req, "capabilities", j_caps);

    char* body_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body_str) {
        return HttpOutcome::ProtocolError;
    }

    HttpResponseBuffer resp_buf{};
    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.event_handler = &HttpEventHandler;
    cfg.user_data = &resp_buf;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        cJSON_free(body_str);
        return HttpOutcome::NetworkError;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!device_token.empty()) {
        const std::string auth = "Bearer " + device_token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }
    esp_http_client_set_post_field(client, body_str, static_cast<int>(std::strlen(body_str)));

    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(body_str);

    if (out_retry_after) {
        *out_retry_after = resp_buf.retry_after_sec;
    }

    // Note: esp_http_client returns ESP_ERR_NOT_SUPPORTED on HTTP 401 when auth_type is NONE.
    if (err != ESP_OK && status != 401) {
        ESP_LOGW(kTag, "POST %s failed: %s (http=%d)", url.c_str(), esp_err_to_name(err), status);
        return HttpOutcome::NetworkError;
    }

    cJSON* root = cJSON_Parse(resp_buf.body.c_str());
    const std::string err_code = ExtractErrorCode(root);
    ESP_LOGI(kTag, "POST /device/v1/bootstrap -> HTTP %d (code=%s)",
             status, err_code.empty() ? "-" : err_code.c_str());

    if (status == 401 || err_code == "REAUTH_REQUIRED") {
        cJSON_Delete(root);
        return HttpOutcome::ReauthRequired;
    }
    if (status == 409) {
        cJSON_Delete(root);
        if (err_code == "DEVICE_ALREADY_BOUND") return HttpOutcome::AlreadyBound;
        return HttpOutcome::PairingInProgress;
    }
    if (status == 429) {
        cJSON_Delete(root);
        return HttpOutcome::RateLimited;
    }
    if (status != 200 || !root) {
        cJSON_Delete(root);
        return HttpOutcome::ProtocolError;
    }

    const cJSON* j_time = cJSON_GetObjectItem(root, "server_time_ms");
    if (cJSON_IsNumber(j_time)) {
        SyncRtcFromServerMs(static_cast<int64_t>(j_time->valuedouble));
    }

    const cJSON* j_status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(j_status) || !j_status->valuestring) {
        cJSON_Delete(root);
        return HttpOutcome::ProtocolError;
    }

    if (std::strcmp(j_status->valuestring, "bound") == 0) {
        const cJSON* j_uid = cJSON_GetObjectItem(root, "device_uid");
        const cJSON* j_ws = cJSON_GetObjectItem(root, "ws_url");
        const cJSON* j_rot = cJSON_GetObjectItem(root, "rotated_device_token");
        const cJSON* j_ota = cJSON_GetObjectItem(root, "ota");

        if (cJSON_IsString(j_uid) && j_uid->valuestring) {
            device_uid_ = j_uid->valuestring;
        }
        ws_url_ = (cJSON_IsString(j_ws) && j_ws->valuestring) ? j_ws->valuestring : "";

        if (cJSON_IsString(j_rot) && j_rot->valuestring && j_rot->valuestring[0] != '\0') {
            ESP_LOGI(kTag, "Server rotated device_token; persisting to NVS");
            store_->SaveDeviceCredentials(device_uid_, j_rot->valuestring);
        }

        ota_ = OtaMetadata{};
        if (cJSON_IsObject(j_ota)) {
            const cJSON* lv = cJSON_GetObjectItem(j_ota, "latest_version");
            const cJSON* mand = cJSON_GetObjectItem(j_ota, "mandatory");
            const cJSON* ota_url = cJSON_GetObjectItem(j_ota, "url");
            const cJSON* sha = cJSON_GetObjectItem(j_ota, "sha256");
            if (cJSON_IsString(lv) && cJSON_IsString(ota_url)) {
                ota_.available = true;
                ota_.latest_version = lv->valuestring;
                ota_.mandatory = cJSON_IsBool(mand) && cJSON_IsTrue(mand);
                ota_.url = ota_url->valuestring;
                if (cJSON_IsString(sha)) ota_.sha256 = sha->valuestring;
                ESP_LOGI(kTag, "OTA metadata available: v%s (mandatory=%d)",
                         ota_.latest_version.c_str(), ota_.mandatory);
            }
        }

        bound_ = true;
        if (out_is_bound) *out_is_bound = true;
        cJSON_Delete(root);
        return HttpOutcome::Ok;
    }

    if (std::strcmp(j_status->valuestring, "unbound") == 0) {
        const cJSON* j_pid = cJSON_GetObjectItem(root, "pairing_id");
        const cJSON* j_code = cJSON_GetObjectItem(root, "pairing_code");
        const cJSON* j_exp = cJSON_GetObjectItem(root, "expires_in");
        const cJSON* j_poll = cJSON_GetObjectItem(root, "poll_interval_sec");

        if (!cJSON_IsString(j_pid) || !cJSON_IsString(j_code)) {
            cJSON_Delete(root);
            return HttpOutcome::ProtocolError;
        }
        if (out_pairing_id) *out_pairing_id = j_pid->valuestring;
        if (out_pairing_code) *out_pairing_code = j_code->valuestring;
        if (out_expires_in) *out_expires_in = cJSON_IsNumber(j_exp) ? j_exp->valueint : 300;
        if (out_poll_sec) *out_poll_sec = cJSON_IsNumber(j_poll) ? j_poll->valueint : kDefaultPollSec;
        if (out_is_bound) *out_is_bound = false;
        bound_ = false;
        cJSON_Delete(root);
        return HttpOutcome::Ok;
    }

    cJSON_Delete(root);
    return HttpOutcome::ProtocolError;
}

BootstrapClient::HttpOutcome BootstrapClient::CallPairingStatus(
    const std::string& server_url,
    const std::string& pairing_id,
    const std::string& claim_secret,
    std::string* out_uid,
    std::string* out_token,
    std::string* out_ws_url,
    int* out_expires_in,
    int* out_poll_sec,
    int* out_retry_after,
    bool* out_claimed) {
    const std::string url = server_url + "/device/v1/pairing-status";

    HttpResponseBuffer resp_buf{};
    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.event_handler = &HttpEventHandler;
    cfg.user_data = &resp_buf;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return HttpOutcome::NetworkError;
    }

    // Pass pairing proof strictly via HTTP headers (never in URL query string)
    esp_http_client_set_header(client, "X-Pairing-Id", pairing_id.c_str());
    esp_http_client_set_header(client, "X-Claim-Secret", claim_secret.c_str());

    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (out_retry_after) {
        *out_retry_after = resp_buf.retry_after_sec;
    }

    if (err != ESP_OK && status != 401) {
        ESP_LOGW(kTag, "GET %s failed: %s (http=%d)", url.c_str(), esp_err_to_name(err), status);
        return HttpOutcome::NetworkError;
    }

    cJSON* root = cJSON_Parse(resp_buf.body.c_str());
    const std::string err_code = ExtractErrorCode(root);

    if (status == 410 || err_code == "PAIRING_EXPIRED") {
        cJSON_Delete(root);
        return HttpOutcome::Expired;
    }
    if (status == 401 || err_code == "PAIRING_PROOF_INVALID") {
        cJSON_Delete(root);
        return HttpOutcome::ProofInvalid;
    }
    if (status == 429) {
        cJSON_Delete(root);
        return HttpOutcome::RateLimited;
    }
    if (status != 200 || !root) {
        cJSON_Delete(root);
        return HttpOutcome::ProtocolError;
    }

    const cJSON* j_status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsString(j_status) || !j_status->valuestring) {
        cJSON_Delete(root);
        return HttpOutcome::ProtocolError;
    }

    if (std::strcmp(j_status->valuestring, "claimed") == 0) {
        const cJSON* j_uid = cJSON_GetObjectItem(root, "device_uid");
        const cJSON* j_tok = cJSON_GetObjectItem(root, "device_token");
        const cJSON* j_ws = cJSON_GetObjectItem(root, "ws_url");
        if (!cJSON_IsString(j_uid) || !cJSON_IsString(j_tok)) {
            cJSON_Delete(root);
            return HttpOutcome::ProtocolError;
        }
        if (out_uid) *out_uid = j_uid->valuestring;
        if (out_token) *out_token = j_tok->valuestring;
        if (out_ws_url) {
            *out_ws_url = (cJSON_IsString(j_ws) && j_ws->valuestring) ? j_ws->valuestring : "";
        }
        if (out_claimed) *out_claimed = true;
        cJSON_Delete(root);
        return HttpOutcome::Ok;
    }

    if (std::strcmp(j_status->valuestring, "pending") == 0) {
        const cJSON* j_exp = cJSON_GetObjectItem(root, "expires_in");
        const cJSON* j_poll = cJSON_GetObjectItem(root, "poll_interval_sec");
        if (out_expires_in) *out_expires_in = cJSON_IsNumber(j_exp) ? j_exp->valueint : 0;
        if (out_poll_sec) *out_poll_sec = cJSON_IsNumber(j_poll) ? j_poll->valueint : kDefaultPollSec;
        if (out_claimed) *out_claimed = false;
        cJSON_Delete(root);
        return HttpOutcome::Ok;
    }

    if (std::strcmp(j_status->valuestring, "expired") == 0) {
        cJSON_Delete(root);
        return HttpOutcome::Expired;
    }

    cJSON_Delete(root);
    return HttpOutcome::ProtocolError;
}

void BootstrapClient::RunLoop() {
    auto sleep_interruptible = [this](int seconds) {
        const int slices = seconds * 10;
        for (int i = 0; i < slices && running_ && !refresh_requested_; ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    };

    while (running_) {
        refresh_requested_ = false;
        if (!wifi_ || !wifi_->is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        const std::string server_url = store_->LoadServerBaseUrl();
        if (server_url.find("agentsphere.example") != std::string::npos) {
            ESP_LOGW(kTag, "Server URL is still placeholder (%s); set CONFIG_AS_SERVER_BASE_URL",
                     server_url.c_str());
            EmitErrorEvent("未配置服务器地址");
            sleep_interruptible(15);
            continue;
        }

        // Step 1: If already bound in NVS, verify with authenticated POST /device/v1/bootstrap
        std::string uid, token;
        if (store_->LoadDeviceCredentials(&uid, &token)) {
            bool is_bound = false;
            int retry_after = 0;
            const HttpOutcome res = CallBootstrap(server_url, token, "", nullptr, nullptr,
                                                  nullptr, nullptr, &retry_after, &is_bound);
            if (res == HttpOutcome::Ok && is_bound) {
                const char* detail = ws_url_.empty() ? "已绑定（待接入语音）" : "已绑定并就绪";
                ESP_LOGI(kTag, "Device bound: uid=%s ws_url=%s",
                         device_uid_.c_str(), ws_url_.empty() ? "(null)" : ws_url_.c_str());
                EmitBoundEvent(detail);
                // Stay idle until refresh requested (e.g. re-check) or stopped
                while (running_ && !refresh_requested_) {
                    vTaskDelay(pdMS_TO_TICKS(250));
                }
                continue;
            }
            if (res == HttpOutcome::ReauthRequired) {
                ESP_LOGW(kTag, "Stored device_token rejected (REAUTH_REQUIRED); clearing NVS credentials");
                store_->ClearDeviceCredentials();
                store_->ClearPairingProof();
                bound_ = false;
                // Fall through immediately to unbound pairing flow
            } else if (res == HttpOutcome::RateLimited) {
                sleep_interruptible(retry_after > 0 ? retry_after : 10);
                continue;
            } else {
                EmitErrorEvent("服务器连接失败，重试中...");
                sleep_interruptible(10);
                continue;
            }
        }

        // Step 2: Unbound flow — first check if we have a persisted pairing proof to recover
        std::string pairing_id, claim_secret, pairing_code;
        int expires_in = 300;
        int poll_sec = kDefaultPollSec;
        bool need_new_bootstrap = true;

        if (store_->LoadPairingProof(&pairing_id, &claim_secret)) {
            ESP_LOGI(kTag, "Found saved pairing proof in NVS; checking recovery status...");
            std::string claimed_uid, claimed_tok, claimed_ws;
            bool claimed = false;
            int retry_after = 0;
            const HttpOutcome st = CallPairingStatus(server_url, pairing_id, claim_secret,
                                                     &claimed_uid, &claimed_tok, &claimed_ws,
                                                     &expires_in, &poll_sec, &retry_after, &claimed);
            if (st == HttpOutcome::Ok && claimed) {
                ESP_LOGI(kTag, "Recovered claimed credentials from NVS proof! uid=%s",
                         claimed_uid.c_str());
                store_->SaveDeviceCredentials(claimed_uid, claimed_tok);
                store_->ClearPairingProof();
                // Loop back to Step 1 so we immediately send authenticated Bootstrap to close recovery window
                continue;
            }
            if (st == HttpOutcome::Expired || st == HttpOutcome::ProofInvalid) {
                store_->ClearPairingProof();
                pairing_id.clear();
                claim_secret.clear();
            } else if (st == HttpOutcome::Ok && !claimed) {
                // Not claimed yet (and 6-digit code is no longer in RAM after reboot or refresh):
                // clear old pending proof and immediately request a fresh visible 6-digit code.
                ESP_LOGI(kTag, "Saved pairing still unclaimed (%ds left); requesting fresh code", expires_in);
                store_->ClearPairingProof();
                pairing_id.clear();
                claim_secret.clear();
            }
        }

        // Step 3: Request fresh 6-digit pairing code via unbound POST /device/v1/bootstrap
        if (need_new_bootstrap) {
            claim_secret = GenerateClaimSecretHex();
            bool is_bound = false;
            int retry_after = 0;
            const HttpOutcome res = CallBootstrap(server_url, "", claim_secret,
                                                  &pairing_id, &pairing_code,
                                                  &expires_in, &poll_sec, &retry_after, &is_bound);
            if (res == HttpOutcome::AlreadyBound) {
                ESP_LOGW(kTag, "Hardware is still bound to an account (DEVICE_ALREADY_BOUND)");
                EmitPairingEvent("已绑定", "请先在网页端解绑后刷新", 0);
                while (running_ && !refresh_requested_) {
                    vTaskDelay(pdMS_TO_TICKS(250));
                }
                continue;
            }
            if (res == HttpOutcome::PairingInProgress || res == HttpOutcome::RateLimited) {
                const int wait_s = retry_after > 0 ? retry_after : 10;
                char msg[64];
                std::snprintf(msg, sizeof(msg), "服务繁忙，%d 秒后重试", wait_s);
                EmitErrorEvent(msg);
                sleep_interruptible(wait_s);
                continue;
            }
            if (res != HttpOutcome::Ok || is_bound) {
                EmitErrorEvent("配对请求失败，重试中...");
                sleep_interruptible(8);
                continue;
            }

            // Persist pairing proof BEFORE displaying code so reboot cannot lose a claimed token
            store_->SavePairingProof(pairing_id, claim_secret);
            ESP_LOGI(kTag, "Pairing code issued: %s (expires_in=%ds, poll=%ds)",
                     pairing_code.c_str(), expires_in, poll_sec);
            EmitPairingEvent(pairing_code.c_str(), "请在网页端输入配对码", expires_in);
        }

        // Step 4: Poll GET /device/v1/pairing-status until claimed, expired, or refreshed
        while (running_ && !refresh_requested_) {
            sleep_interruptible(poll_sec > 0 ? poll_sec : kDefaultPollSec);
            if (!running_ || refresh_requested_) break;

            std::string claimed_uid, claimed_tok, claimed_ws;
            bool claimed = false;
            int retry_after = 0;
            const HttpOutcome st = CallPairingStatus(server_url, pairing_id, claim_secret,
                                                     &claimed_uid, &claimed_tok, &claimed_ws,
                                                     &expires_in, &poll_sec, &retry_after, &claimed);
            if (st == HttpOutcome::Ok && claimed) {
                ESP_LOGI(kTag, "Device claimed successfully! uid=%s", claimed_uid.c_str());
                // Save token durably BEFORE clearing pairing proof
                store_->SaveDeviceCredentials(claimed_uid, claimed_tok);
                store_->ClearPairingProof();
                // Break to Step 1 to immediately send authenticated Bootstrap (closes recovery window)
                break;
            }
            if (st == HttpOutcome::Ok && !claimed) {
                char hint[64];
                std::snprintf(hint, sizeof(hint), "请在网页端输入（剩余 %d 秒）", expires_in);
                EmitPairingEvent(pairing_code.empty() ? "------" : pairing_code.c_str(),
                                 hint, expires_in);
                continue;
            }
            if (st == HttpOutcome::Expired) {
                ESP_LOGW(kTag, "Pairing code expired");
                store_->ClearPairingProof();
                EmitPairingEvent("已过期", "配对码已过期，请点击刷新", 0);
                while (running_ && !refresh_requested_) {
                    vTaskDelay(pdMS_TO_TICKS(250));
                }
                break;
            }
            if (st == HttpOutcome::ProofInvalid) {
                ESP_LOGW(kTag, "Pairing proof invalid; restarting bootstrap");
                store_->ClearPairingProof();
                break;
            }
            if (st == HttpOutcome::RateLimited) {
                sleep_interruptible(retry_after > 0 ? retry_after : 10);
                continue;
            }
            // Transient network error: keep polling without losing pairing_code
            ESP_LOGW(kTag, "Transient error polling pairing-status; will retry");
        }
    }
}

}  // namespace agentsphere
