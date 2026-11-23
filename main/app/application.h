#pragma once

#include "app/events.h"
#include "app/state_machine.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "net/provisioning.h"
#include "net/wifi_manager.h"
#include "protocol/bootstrap_client.h"
#include "storage/settings_store.h"

namespace agentsphere {

class Application {
public:
    esp_err_t Init();
    [[noreturn]] void Run();

private:
    void HandleEvent(const AppEvent& ev);
    void HandleTick1Hz();
    void EnterProvisioning();
    void ExitProvisioningToState(DeviceState target_state, const char* status_text);
    void LogMemoryWatermarks() const;

    QueueHandle_t event_queue_ = nullptr;
    StateMachine sm_;
    SettingsStore settings_;
    WifiManager wifi_;
    ProvisioningServer provisioning_;
    BootstrapClient bootstrap_;

    bool screen_on_ = true;
    uint32_t tick_count_ = 0;
    int emotion_reset_ticks_ = 0;
    int wifi_connect_timeout_ticks_ = 0;
    int offline_retry_ticks_ = 0;
    int pairing_remaining_sec_ = 0;
    char pairing_code_active_[16]{};
    TouchPoint last_touch_{};
    PowerStatus last_power_{};
    char last_event_text_[96] = "启动完成";
};

}  // namespace agentsphere
