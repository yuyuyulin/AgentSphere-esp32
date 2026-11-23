#include "app/application.h"

#include <cstdio>
#include <cstring>

#include "boards/board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ui/display.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "App";
constexpr uint32_t kEventQueueLen = 32;
constexpr int kWifiConnectTimeoutSec = 18;
constexpr int kOfflineRetryIntervalSec = 30;
}  // namespace

esp_err_t Application::Init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS requires erase, reinitializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(settings_.Init());

    event_queue_ = xQueueCreate(kEventQueueLen, sizeof(AppEvent));
    if (!event_queue_) {
        ESP_LOGE(kTag, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    Board& board = Board::GetInstance();
    ESP_LOGI(kTag, "Booting board: %s", board.GetBoardName());

    sm_.SetObserver([](DeviceState /*old_state*/, DeviceState new_state) {
        if (auto* display = Board::GetInstance().GetDisplay()) {
            display->SetState(new_state);
        }
    });

    err = board.Init(event_queue_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Board init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Mark running OTA image valid only after board initialization succeeds.
    esp_ota_mark_app_valid_cancel_rollback();

    if (auto* audio = board.GetAudioCodec(); audio && audio->IsReady()) {
        audio->SetOutputVolume(95);
        audio->PlayTone(880, 140);
        audio->PlayTone(1175, 160);
    }

    ESP_ERROR_CHECK(wifi_.Init(&settings_, event_queue_));
    bootstrap_.Init(&settings_, &wifi_, event_queue_);

    // Startup Wi-Fi policy:
    // - First boot (no saved Wi-Fi): enter SoftAP provisioning immediately.
    // - Subsequent boot: scan and connect highest-priority saved Wi-Fi with 18s timeout.
    if (wifi_.ConnectSavedNetworks() == ESP_ERR_NOT_FOUND) {
        EnterProvisioning();
    } else {
        wifi_connect_timeout_ticks_ = kWifiConnectTimeoutSec;
        if (auto* display = board.GetDisplay()) {
            display->SetState(DeviceState::Starting, "Connecting Wi-Fi...");
            display->SetEmotion(Emotion::Thinking);
        }
    }

    LogMemoryWatermarks();
    return ESP_OK;
}

void Application::Run() {
    TickType_t last_tick_event = xTaskGetTickCount();

    while (true) {
        AppEvent ev{};
        if (xQueueReceive(event_queue_, &ev, pdMS_TO_TICKS(100)) == pdTRUE) {
            HandleEvent(ev);
        }

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_tick_event) >= pdMS_TO_TICKS(1000)) {
            last_tick_event = now;
            HandleTick1Hz();
        }
    }
}

void Application::EnterProvisioning() {
    bootstrap_.Stop();
    wifi_connect_timeout_ticks_ = 0;
    offline_retry_ticks_ = 0;
    sm_.TransitionTo(DeviceState::WifiConfiguring);

    if (provisioning_.Start(&wifi_, &settings_, event_queue_) == ESP_OK) {
        std::snprintf(last_event_text_, sizeof(last_event_text_), "AP: %s",
                      provisioning_.ap_ssid().c_str());
        if (auto* display = Board::GetInstance().GetDisplay()) {
            display->ShowPairingCode(nullptr, nullptr);
            display->ShowProvisioning(provisioning_.ap_ssid().c_str(), "192.168.4.1");
        }
    }
}

void Application::ExitProvisioningToState(DeviceState target_state, const char* status_text) {
    provisioning_.Stop();
    if (auto* display = Board::GetInstance().GetDisplay()) {
        display->ShowProvisioning(nullptr, nullptr);
    }
    sm_.TransitionTo(target_state);
    if (auto* display = Board::GetInstance().GetDisplay()) {
        display->SetState(target_state, status_text);
        display->SetEmotion(target_state == DeviceState::Idle ? Emotion::Happy : Emotion::Neutral);
    }
    emotion_reset_ticks_ = 2;
}

void Application::HandleEvent(const AppEvent& ev) {
    Board& board = Board::GetInstance();
    auto* display = board.GetDisplay();
    auto* audio = board.GetAudioCodec();
    auto* power = board.GetPowerManager();

    switch (ev.type) {
        case AppEventType::WifiConnected: {
            wifi_connect_timeout_ticks_ = 0;
            offline_retry_ticks_ = 0;
            std::snprintf(last_event_text_, sizeof(last_event_text_), "Wi-Fi: %s", ev.ssid);
            if (display) {
                display->SetWifiStatus(true, ev.ssid, ev.ip);
            }
            if (provisioning_.is_running()) {
                ExitProvisioningToState(DeviceState::Activating, "Checking Binding...");
            } else {
                sm_.TransitionTo(DeviceState::Activating);
                if (display) {
                    display->SetState(DeviceState::Activating, "Checking Binding...");
                    display->SetEmotion(Emotion::Thinking);
                }
            }
            bootstrap_.Start();
            LogMemoryWatermarks();
            break;
        }
        case AppEventType::BootstrapBound: {
            std::snprintf(last_event_text_, sizeof(last_event_text_), "%s", ev.text);
            if (display) {
                display->ShowPairingCode(nullptr, nullptr);
            }
            sm_.TransitionTo(DeviceState::Idle);
            if (display) {
                display->SetState(DeviceState::Idle, ev.text);
                display->SetEmotion(Emotion::Happy);
            }
            emotion_reset_ticks_ = 2;
            if (audio && audio->IsReady()) {
                audio->PlayTone(1318, 120);
            }
            break;
        }
        case AppEventType::BootstrapPairingCode: {
            std::snprintf(last_event_text_, sizeof(last_event_text_), "Code: %s", ev.pairing_code);
            sm_.TransitionTo(DeviceState::Activating);
            if (display) {
                display->ShowPairingCode(ev.pairing_code, ev.text);
            }
            break;
        }
        case AppEventType::BootstrapError: {
            std::snprintf(last_event_text_, sizeof(last_event_text_), "%s", ev.text);
            if (display) {
                display->SetState(sm_.state(), ev.text);
                display->SetEmotion(Emotion::Confused);
            }
            break;
        }
        case AppEventType::PairingRefreshRequested: {
            std::snprintf(last_event_text_, sizeof(last_event_text_), "Refreshing pairing...");
            bootstrap_.RequestRefresh();
            break;
        }
        case AppEventType::WifiDisconnected:
        case AppEventType::WifiConnectTimeout: {
            wifi_connect_timeout_ticks_ = 0;
            if (sm_.state() == DeviceState::WifiConfiguring) {
                std::snprintf(last_event_text_, sizeof(last_event_text_), "Wi-Fi failed, retry in portal");
                break;
            }
            bootstrap_.Stop();
            std::snprintf(last_event_text_, sizeof(last_event_text_), "Offline (Hold BOOT to setup)");
            sm_.TransitionTo(DeviceState::Offline);
            if (display) {
                display->ShowPairingCode(nullptr, nullptr);
                display->SetWifiStatus(false, nullptr, nullptr);
                display->SetState(DeviceState::Offline, "Hold BOOT to setup");
                display->SetEmotion(Emotion::Confused);
            }
            offline_retry_ticks_ = kOfflineRetryIntervalSec;
            break;
        }
        case AppEventType::ProvisioningConnectRequested: {
            std::snprintf(last_event_text_, sizeof(last_event_text_), "Connecting: %s", ev.ssid);
            wifi_connect_timeout_ticks_ = kWifiConnectTimeoutSec;
            break;
        }
        case AppEventType::ProvisioningSkipped: {
            std::snprintf(last_event_text_, sizeof(last_event_text_), "Skipped -> Offline");
            if (display) {
                display->SetWifiStatus(false, nullptr, nullptr);
            }
            ExitProvisioningToState(DeviceState::Offline, "Offline Mode");
            offline_retry_ticks_ = kOfflineRetryIntervalSec;
            LogMemoryWatermarks();
            break;
        }
        case AppEventType::Button: {
            if (ev.button_id == ButtonId::Power) {
                if (ev.button_action == ButtonAction::ShortPress) {
                    screen_on_ = !screen_on_;
                    std::snprintf(last_event_text_, sizeof(last_event_text_), "PWR short -> %s",
                                  screen_on_ ? "Screen ON" : "Screen OFF");
                    ESP_LOGI(kTag, "%s", last_event_text_);
                    if (display) {
                        display->SetScreenOn(screen_on_);
                    }
                } else if (ev.button_action == ButtonAction::LongPress) {
                    std::snprintf(last_event_text_, sizeof(last_event_text_), "PWR long -> PowerOff");
                    ESP_LOGW(kTag, "%s", last_event_text_);
                    if (power) {
                        power->PowerOff();
                    }
                }
            } else if (ev.button_id == ButtonId::Boot) {
                if (ev.button_action == ButtonAction::ShortPress) {
                    if (!screen_on_) {
                        screen_on_ = true;
                        if (display) {
                            display->SetScreenOn(true);
                        }
                    }
                    if (sm_.state() == DeviceState::WifiConfiguring) {
                        // Short press BOOT during provisioning skips to offline mode
                        AppEvent skip_ev{};
                        skip_ev.type = AppEventType::ProvisioningSkipped;
                        HandleEvent(skip_ev);
                        break;
                    }
                    std::snprintf(last_event_text_, sizeof(last_event_text_), "BOOT -> Audio Loopback");
                    ESP_LOGI(kTag, "BOOT short press: triggering 2s audio loopback self-test");
                    if (display) {
                        display->SetState(DeviceState::Listening, "Rec 2s -> Playback");
                        display->SetEmotion(Emotion::Surprised);
                    }
                    if (audio && audio->IsReady()) {
                        audio->PlayTone(1046, 80);
                        audio->RecordAndLoopback(2000);
                    }
                    if (display) {
                        const DeviceState back = wifi_.is_connected() ? DeviceState::Idle : DeviceState::Offline;
                        display->SetState(back, "Loopback Done");
                        display->SetEmotion(Emotion::Happy);
                        emotion_reset_ticks_ = 2;
                    }
                } else if (ev.button_action == ButtonAction::LongPress) {
                    std::snprintf(last_event_text_, sizeof(last_event_text_), "BOOT long -> Wi-Fi Setup");
                    ESP_LOGI(kTag, "%s", last_event_text_);
                    EnterProvisioning();
                }
            }
            break;
        }
        case AppEventType::Touch: {
            last_touch_ = ev.touch;
            if (ev.touch.pressed) {
                std::snprintf(last_event_text_, sizeof(last_event_text_), "Touch (%d,%d)",
                              ev.touch.x, ev.touch.y);
                if (display) {
                    display->SetEmotion(Emotion::Happy);
                }
                emotion_reset_ticks_ = 2;
            }
            break;
        }
        case AppEventType::ImuGesture: {
            if (ev.imu_gesture == ImuGesture::Shake) {
                std::snprintf(last_event_text_, sizeof(last_event_text_), "IMU: Shake");
                ESP_LOGI(kTag, "%s", last_event_text_);
                if (display) {
                    display->SetEmotion(Emotion::Dizzy);
                }
                emotion_reset_ticks_ = 2;
            } else if (ev.imu_gesture == ImuGesture::FlipFaceDown) {
                std::snprintf(last_event_text_, sizeof(last_event_text_), "IMU: FaceDown -> Mute");
                ESP_LOGI(kTag, "%s", last_event_text_);
                if (audio && audio->IsReady()) {
                    audio->StopOutput();
                }
                screen_on_ = false;
                if (display) {
                    display->SetScreenOn(false);
                }
            } else if (ev.imu_gesture == ImuGesture::PickUp || ev.imu_gesture == ImuGesture::FaceUp) {
                if (!screen_on_) {
                    screen_on_ = true;
                    std::snprintf(last_event_text_, sizeof(last_event_text_), "IMU: Wake Screen");
                    ESP_LOGI(kTag, "%s", last_event_text_);
                    if (display) {
                        display->SetScreenOn(true);
                    }
                }
            }
            break;
        }
        case AppEventType::PowerChanged: {
            last_power_ = ev.power;
            if (display) {
                display->SetPowerStatus(ev.power);
            }
            break;
        }
        case AppEventType::AudioTestTrigger: {
            if (audio && audio->IsReady()) {
                audio->PlayTone(660, 120);
            }
            break;
        }
        case AppEventType::Tick1Hz:
            HandleTick1Hz();
            break;
    }
}

void Application::HandleTick1Hz() {
    ++tick_count_;
    Board& board = Board::GetInstance();

    if (emotion_reset_ticks_ > 0) {
        --emotion_reset_ticks_;
        if (emotion_reset_ticks_ == 0) {
            if (auto* display = board.GetDisplay()) {
                display->SetEmotion(Emotion::Neutral);
            }
        }
    }

    if (wifi_connect_timeout_ticks_ > 0) {
        --wifi_connect_timeout_ticks_;
        if (wifi_connect_timeout_ticks_ == 0 && !wifi_.is_connected()) {
            AppEvent ev{};
            ev.type = AppEventType::WifiConnectTimeout;
            HandleEvent(ev);
        }
    }

    if (sm_.state() == DeviceState::Offline && offline_retry_ticks_ > 0) {
        --offline_retry_ticks_;
        if (offline_retry_ticks_ == 0) {
            if (wifi_.ConnectSavedNetworks() == ESP_OK) {
                wifi_connect_timeout_ticks_ = kWifiConnectTimeoutSec;
            } else {
                offline_retry_ticks_ = kOfflineRetryIntervalSec;
            }
        }
    }

    PowerStatus power{};
    if (auto* pm = board.GetPowerManager()) {
        power = pm->ReadStatus();
        last_power_ = power;
    }

    ImuData imu{};
    if (auto* imu_sensor = board.GetImuSensor()) {
        imu = imu_sensor->Read();
        if (imu.valid) {
            const ImuGesture gesture = imu_sensor->DetectGesture(imu);
            if (gesture != ImuGesture::None) {
                AppEvent ge{};
                ge.type = AppEventType::ImuGesture;
                ge.imu_gesture = gesture;
                HandleEvent(ge);
            }
        }
    }

    RtcTime rtc{};
    if (auto* clock = board.GetRtcClock()) {
        rtc = clock->ReadTime();
    }

    if (auto* display = board.GetDisplay()) {
        display->SetPowerStatus(power);
        display->SetDiagnosticTelemetry(imu, rtc, last_touch_, last_event_text_);
    }

    if ((tick_count_ % 10) == 0) {
        ESP_LOGI(kTag,
                 "Telemetry | state=%s wifi=%d(%s) | pwr(usb=%d chg=%d soc=%d%% %dmV) | imu(%d: %.2f,%.2f,%.2f)",
                 DeviceStateToString(sm_.state()),
                 wifi_.is_connected(),
                 wifi_.is_connected() ? wifi_.current_ip().c_str() : "offline",
                 power.usb_plugged, power.charging, power.battery_percent, power.battery_mv,
                 imu.valid, imu.ax, imu.ay, imu.az);
        LogMemoryWatermarks();
    }
}

void Application::LogMemoryWatermarks() const {
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t min_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t min_psram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(kTag, "Heap | Internal: %u KB (min %u KB) | PSRAM: %u KB (min %u KB)",
             static_cast<unsigned>(free_internal / 1024),
             static_cast<unsigned>(min_internal / 1024),
             static_cast<unsigned>(free_psram / 1024),
             static_cast<unsigned>(min_psram / 1024));
}

}  // namespace agentsphere
