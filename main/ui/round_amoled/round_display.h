#pragma once

#include "sdkconfig.h"

#if CONFIG_AS_ENABLE_LVGL

#include <atomic>

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
    void SetWifiStatus(bool connected, const char*, const char*) override;
    void SetQuickControls(const QuickControls& controls) override;
    void ShowProvisioning(const char* ap_ssid, const char* ip_addr) override;
    void ShowPairingCode(const char* code, const char* hint = nullptr) override;
    void SetDiagnosticTelemetry(const ImuData&, const RtcTime&,
                                const TouchPoint&, const char*) override {}
    void SetBrightness(uint8_t percent) override;
    void SetScreenOn(bool on) override;

private:
    static void OnHomeTouch(lv_event_t* e);
    static void OnQuickPanelGesture(lv_event_t* e);
    static void OnQuickControl(lv_event_t* e);
    static void OnSkipProvisioningClicked(lv_event_t* e);
    static void OnRefreshPairingClicked(lv_event_t* e);
    void CreateQuickPanel(lv_obj_t* parent);
    void SetQuickPanelVisible(bool visible);
    void UpdateQuickStatus();
    void PostQuickEvent(AppEventType type, int value = 0);
    void UpdatePromptVisibility();
    void ApplyBrightnessLocked();

    QueueHandle_t event_queue_ = nullptr;
    ThemeRenderer theme_;
    std::atomic_bool requested_screen_on_{true};
    DeviceState state_ = DeviceState::Starting;
    Emotion emotion_ = Emotion::Neutral;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* low_battery_label_ = nullptr;
    lv_point_t home_press_{};
    bool home_press_active_ = false;
    lv_obj_t* quick_panel_ = nullptr;
    lv_obj_t* quick_wifi_ = nullptr;
    lv_obj_t* quick_battery_icon_ = nullptr;
    lv_obj_t* quick_battery_ = nullptr;
    lv_obj_t* quick_charge_status_ = nullptr;
    lv_obj_t* quick_volume_ = nullptr;
    lv_obj_t* quick_brightness_ = nullptr;
    lv_obj_t* quick_mic_ = nullptr;
    PowerStatus power_;
    bool wifi_connected_ = false;
    lv_obj_t* prov_panel_ = nullptr;
    lv_obj_t* prov_qr_ = nullptr;
    lv_obj_t* prov_ssid_label_ = nullptr;
    lv_obj_t* prov_url_label_ = nullptr;
    lv_obj_t* pair_panel_ = nullptr;
    lv_obj_t* pair_code_label_ = nullptr;
    lv_obj_t* pair_hint_label_ = nullptr;
    uint8_t brightness_percent_ = 80;
    std::atomic<uint8_t> requested_brightness_{80};
    uint8_t applied_brightness_ = 100;
    bool initialized_ = false;
    bool overlay_active_ = false;
    bool quick_visible_ = false;
    bool low_battery_ = false;
};

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
