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
        audio->SetOutputVolume(70);
        audio->PlayTone(880, 120);
    }

    sm_.TransitionTo(DeviceState::Idle);
    if (auto* display = board.GetDisplay()) {
        display->SetState(DeviceState::Idle, "Step 0 Bring-up Ready");
        display->SetEmotion(Emotion::Neutral);
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

void Application::HandleEvent(const AppEvent& ev) {
    Board& board = Board::GetInstance();
    auto* display = board.GetDisplay();
    auto* audio = board.GetAudioCodec();
    auto* power = board.GetPowerManager();

    switch (ev.type) {
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
                        display->SetState(DeviceState::Idle, "Loopback Done");
                        display->SetEmotion(Emotion::Happy);
                    }
                } else if (ev.button_action == ButtonAction::LongPress) {
                    std::snprintf(last_event_text_, sizeof(last_event_text_), "BOOT long -> Provisioning");
                    ESP_LOGI(kTag, "%s", last_event_text_);
                    sm_.TransitionTo(DeviceState::WifiConfiguring);
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

    if ((tick_count_ % 5) == 0) {
        ESP_LOGI(kTag,
                 "Telemetry | pwr(usb=%d chg=%d soc=%d%% %dmV) | imu(%d: %.2f,%.2f,%.2f) | rtc(%d: %04d-%02d-%02d %02d:%02d:%02d)",
                 power.usb_plugged, power.charging, power.battery_percent, power.battery_mv,
                 imu.valid, imu.ax, imu.ay, imu.az,
                 rtc.valid, rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
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
