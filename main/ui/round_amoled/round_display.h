#pragma once

#include "sdkconfig.h"

#if CONFIG_AS_ENABLE_LVGL

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "ui/display.h"
#include "ui/round_amoled/theme_renderer.h"

namespace agentsphere {

class RoundAmoledDisplay final : public Display {
public:
    explicit RoundAmoledDisplay(QueueHandle_t event_queue);

    esp_err_t Init() override;
    void SetState(DeviceState state, const char* status_text = nullptr) override;
    void SetEmotion(Emotion emotion) override;
    void SetPowerStatus(const PowerStatus& power) override;
    void ShowPairingCode(const char* code) override;
    void SetDiagnosticTelemetry(const ImuData& imu,
                                const RtcTime& rtc,
                                const TouchPoint& touch,
                                const char* last_event) override;
    void SetBrightness(uint8_t percent) override;
    void SetScreenOn(bool on) override;

private:
    static void OnScreenPressed(lv_event_t* e);

    QueueHandle_t event_queue_ = nullptr;
    ThemeRenderer theme_;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* power_label_ = nullptr;
    lv_obj_t* sensor_label_ = nullptr;
    lv_obj_t* event_label_ = nullptr;
    lv_obj_t* pairing_label_ = nullptr;
    lv_obj_t* touch_dot_ = nullptr;
    uint8_t brightness_percent_ = 80;
    bool initialized_ = false;
};

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
