#pragma once

#include <cstdint>
#include <string>

#include "boards/board.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net/wifi_manager.h"
#include "storage/settings_store.h"

namespace agentsphere {

struct OtaMetadata {
    bool available = false;
    bool mandatory = false;
    std::string latest_version;
    std::string url;
    std::string sha256;
};

// Manages HTTPS /device/v1/bootstrap & /device/v1/pairing-status in a dedicated Core 0 task.
// Posts BootstrapBound / BootstrapPairingCode / BootstrapError events to Application.
class BootstrapClient {
public:
    void Init(SettingsStore* store, WifiManager* wifi, QueueHandle_t event_queue);

    // Starts bootstrap/activation flow (idempotent; call when Wi-Fi connects).
    void Start();

    // Requests an immediate fresh pairing code or retry (e.g. user tapped Refresh).
    void RequestRefresh();

    // Stops polling cleanly (e.g. Wi-Fi disconnected or entering provisioning).
    void Stop();

    bool is_bound() const { return bound_; }
    const std::string& device_uid() const { return device_uid_; }
    const std::string& ws_url() const { return ws_url_; }
    const OtaMetadata& ota() const { return ota_; }

    static std::string GenerateClaimSecretHex();

private:
    enum class HttpOutcome {
        Ok = 0,
        ReauthRequired,
        AlreadyBound,
        PairingInProgress,
        Expired,
        ProofInvalid,
        RateLimited,
        NetworkError,
        ProtocolError,
    };

    static void TaskEntry(void* arg);
    void RunLoop();

    HttpOutcome CallBootstrap(const std::string& server_url,
                              const std::string& device_token,
                              const std::string& claim_secret,
                              std::string* out_pairing_id,
                              std::string* out_pairing_code,
                              int* out_expires_in,
                              int* out_poll_sec,
                              int* out_retry_after,
                              bool* out_is_bound);

    HttpOutcome CallPairingStatus(const std::string& server_url,
                                  const std::string& pairing_id,
                                  const std::string& claim_secret,
                                  std::string* out_uid,
                                  std::string* out_token,
                                  std::string* out_ws_url,
                                  int* out_expires_in,
                                  int* out_poll_sec,
                                  int* out_retry_after,
                                  bool* out_claimed);

    void SyncRtcFromServerMs(int64_t server_time_ms);
    void EmitBoundEvent(const char* detail);
    void EmitPairingEvent(const char* code, const char* hint, int expires_in);
    void EmitErrorEvent(const char* message);

    SettingsStore* store_ = nullptr;
    WifiManager* wifi_ = nullptr;
    QueueHandle_t event_queue_ = nullptr;
    TaskHandle_t task_ = nullptr;

    volatile bool running_ = false;
    volatile bool refresh_requested_ = false;
    bool bound_ = false;
    std::string device_uid_;
    std::string ws_url_;
    OtaMetadata ota_;
};

}  // namespace agentsphere
