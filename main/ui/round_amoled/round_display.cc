#include "ui/round_amoled/round_display.h"

#if CONFIG_AS_ENABLE_LVGL

#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "RoundDisplay";
constexpr uint32_t kLockTimeoutMs = 100;
}  // namespace

RoundAmoledDisplay::RoundAmoledDisplay(QueueHandle_t event_queue)
    : event_queue_(event_queue) {}

esp_err_t RoundAmoledDisplay::Init() {
    lv_display_t* disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(kTag, "bsp_display_start failed");
        return ESP_FAIL;
    }

    bsp_display_brightness_set(brightness_percent_);

    if (bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to lock LVGL display");
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_add_event_cb(scr, &RoundAmoledDisplay::OnScreenPressed, LV_EVENT_PRESSED, this);

    theme_.Create(scr);

    status_label_ = lv_label_create(scr);
    lv_label_set_text(status_label_, "XiaoAn | AgentSphere");
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 26);

    wifi_label_ = lv_label_create(scr);
    lv_label_set_text(wifi_label_, "Wi-Fi: starting...");
    lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0x80D8FF), 0);
    lv_obj_align(wifi_label_, LV_ALIGN_CENTER, 0, 54);

    power_label_ = lv_label_create(scr);
    lv_label_set_text(power_label_, "Power: probing...");
    lv_obj_set_style_text_color(power_label_, lv_color_hex(0x80CBC4), 0);
    lv_obj_align(power_label_, LV_ALIGN_CENTER, 0, 82);

    sensor_label_ = lv_label_create(scr);
    lv_label_set_text(sensor_label_, "IMU / RTC: probing...");
    lv_obj_set_style_text_color(sensor_label_, lv_color_hex(0xB0BEC5), 0);
    lv_obj_align(sensor_label_, LV_ALIGN_CENTER, 0, 110);

    event_label_ = lv_label_create(scr);
    lv_label_set_text(event_label_, "Tap screen or press BOOT");
    lv_obj_set_style_text_color(event_label_, lv_color_hex(0xFFE082), 0);
    lv_obj_align(event_label_, LV_ALIGN_CENTER, 0, 138);

    pairing_label_ = lv_label_create(scr);
    lv_label_set_text(pairing_label_, "");
    lv_obj_set_style_text_color(pairing_label_, lv_color_hex(0x80D8FF), 0);
    lv_obj_align(pairing_label_, LV_ALIGN_CENTER, 0, 166);
    lv_obj_set_hidden(pairing_label_, true);

    // Visual touch ripple indicator so every tap produces immediate coordinate feedback
    touch_dot_ = lv_obj_create(scr);
    lv_obj_set_size(touch_dot_, 36, 36);
    lv_obj_set_style_radius(touch_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(touch_dot_, lv_color_hex(0x00E5A0), 0);
    lv_obj_set_style_bg_opa(touch_dot_, LV_OPA_60, 0);
    lv_obj_set_style_border_width(touch_dot_, 3, 0);
    lv_obj_set_style_border_color(touch_dot_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollable(touch_dot_, false);
    lv_obj_set_clickable(touch_dot_, false);
    lv_obj_set_hidden(touch_dot_, true);

    // Full-screen SoftAP Wi-Fi provisioning overlay (hidden by default)
    prov_panel_ = lv_obj_create(scr);
    lv_obj_set_size(prov_panel_, 466, 466);
    lv_obj_center(prov_panel_);
    lv_obj_set_style_radius(prov_panel_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(prov_panel_, lv_color_hex(0x0B1016), 0);
    lv_obj_set_style_bg_opa(prov_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(prov_panel_, 6, 0);
    lv_obj_set_style_border_color(prov_panel_, lv_color_hex(0xFF8A65), 0);
    lv_obj_set_scrollable(prov_panel_, false);

    lv_obj_t* prov_title = lv_label_create(prov_panel_);
    lv_label_set_text(prov_title, "Wi-Fi Setup (2.4GHz)");
    lv_obj_set_style_text_color(prov_title, lv_color_hex(0xFFAB91), 0);
    lv_obj_align(prov_title, LV_ALIGN_TOP_MID, 0, 34);

#if LV_USE_QRCODE
    prov_qr_ = lv_qrcode_create(prov_panel_);
    lv_qrcode_set_size(prov_qr_, 156);
    lv_qrcode_set_dark_color(prov_qr_, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(prov_qr_, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_border_color(prov_qr_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(prov_qr_, 8, 0);
    lv_obj_align(prov_qr_, LV_ALIGN_CENTER, 0, -44);
#endif

    prov_ssid_label_ = lv_label_create(prov_panel_);
    lv_label_set_text(prov_ssid_label_, "AP: AgentSphere-XXXX");
    lv_obj_set_style_text_color(prov_ssid_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(prov_ssid_label_, LV_ALIGN_CENTER, 0, 74);

    prov_url_label_ = lv_label_create(prov_panel_);
    lv_label_set_text(prov_url_label_, "http://192.168.4.1");
    lv_obj_set_style_text_color(prov_url_label_, lv_color_hex(0x80D8FF), 0);
    lv_obj_align(prov_url_label_, LV_ALIGN_CENTER, 0, 102);

    lv_obj_t* skip_btn = lv_button_create(prov_panel_);
    lv_obj_set_size(skip_btn, 160, 44);
    lv_obj_align(skip_btn, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_set_style_radius(skip_btn, 22, 0);
    lv_obj_set_style_bg_color(skip_btn, lv_color_hex(0x263238), 0);
    lv_obj_add_event_cb(skip_btn, &RoundAmoledDisplay::OnSkipProvisioningClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* skip_lbl = lv_label_create(skip_btn);
    lv_label_set_text(skip_lbl, "Offline Mode");
    lv_obj_center(skip_lbl);

    lv_obj_set_hidden(prov_panel_, true);

    bsp_display_unlock();

    initialized_ = true;
    ESP_LOGI(kTag, "466x466 Round AMOLED display initialized (brightness=%u%%)", brightness_percent_);
    return ESP_OK;
}

void RoundAmoledDisplay::OnScreenPressed(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    if (!self || !self->event_queue_) {
        return;
    }

    lv_indev_t* indev = lv_indev_active();
    lv_point_t pt{0, 0};
    if (indev) {
        lv_indev_get_point(indev, &pt);
    }

    // Immediate visual feedback right inside LVGL callback (0ms latency)
    if (self->touch_dot_) {
        lv_obj_set_pos(self->touch_dot_, pt.x - 18, pt.y - 18);
        lv_obj_set_hidden(self->touch_dot_, false);
    }
    self->theme_.SetEmotion(Emotion::Happy);

    AppEvent ev{};
    ev.type = AppEventType::Touch;
    ev.touch.pressed = true;
    ev.touch.x = static_cast<int16_t>(pt.x);
    ev.touch.y = static_cast<int16_t>(pt.y);
    xQueueSend(self->event_queue_, &ev, 0);
}

void RoundAmoledDisplay::SetState(DeviceState state, const char* status_text) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    theme_.SetState(state);
    if (status_label_) {
        char buf[64];
        if (status_text && status_text[0] != '\0') {
            std::snprintf(buf, sizeof(buf), "%s | %s", DeviceStateToString(state), status_text);
        } else {
            std::snprintf(buf, sizeof(buf), "%s", DeviceStateToString(state));
        }
        lv_label_set_text(status_label_, buf);
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetEmotion(Emotion emotion) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    theme_.SetEmotion(emotion);
    if (emotion == Emotion::Neutral && touch_dot_) {
        lv_obj_set_hidden(touch_dot_, true);
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetPowerStatus(const PowerStatus& power) {
    if (!initialized_ || !power_label_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    char buf[64];
    if (power.available) {
        std::snprintf(buf, sizeof(buf), "%s%s %d%% (%dmV)",
                      power.usb_plugged ? "USB " : "BAT ",
                      power.charging ? "CHG" : "IDLE",
                      power.battery_percent,
                      power.battery_mv);
    } else {
        std::snprintf(buf, sizeof(buf), "PMIC: N/A");
    }
    lv_label_set_text(power_label_, buf);
    bsp_display_unlock();
}

void RoundAmoledDisplay::OnSkipProvisioningClicked(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    if (!self || !self->event_queue_) {
        return;
    }
    AppEvent ev{};
    ev.type = AppEventType::ProvisioningSkipped;
    xQueueSend(self->event_queue_, &ev, 0);
}

void RoundAmoledDisplay::SetWifiStatus(bool connected, const char* ssid, const char* ip) {
    if (!initialized_ || !wifi_label_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    char buf[72];
    if (connected) {
        std::snprintf(buf, sizeof(buf), "Wi-Fi: %s (%s)", ssid ? ssid : "", ip ? ip : "");
        lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0x69F0AE), 0);
    } else {
        std::snprintf(buf, sizeof(buf), "Offline (Hold BOOT to setup)");
        lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0xFFAB91), 0);
    }
    lv_label_set_text(wifi_label_, buf);
    bsp_display_unlock();
}

void RoundAmoledDisplay::ShowProvisioning(const char* ap_ssid, const char* ip_addr) {
    if (!initialized_ || !prov_panel_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    if (ap_ssid && ap_ssid[0] != '\0') {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "AP: %s", ap_ssid);
        lv_label_set_text(prov_ssid_label_, buf);

        std::snprintf(buf, sizeof(buf), "http://%s", ip_addr ? ip_addr : "192.168.4.1");
        lv_label_set_text(prov_url_label_, buf);

#if LV_USE_QRCODE
        if (prov_qr_) {
            char qr_payload[96];
            std::snprintf(qr_payload, sizeof(qr_payload), "WIFI:S:%s;T:nopass;;", ap_ssid);
            lv_qrcode_update(prov_qr_, qr_payload, std::strlen(qr_payload));
        }
#endif
        lv_obj_set_hidden(prov_panel_, false);
    } else {
        lv_obj_set_hidden(prov_panel_, true);
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::ShowPairingCode(const char* code) {
    if (!initialized_ || !pairing_label_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    if (code && code[0] != '\0') {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "CODE: %s", code);
        lv_label_set_text(pairing_label_, buf);
        lv_obj_set_hidden(pairing_label_, false);
    } else {
        lv_obj_set_hidden(pairing_label_, true);
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetDiagnosticTelemetry(const ImuData& imu,
                                                const RtcTime& rtc,
                                                const TouchPoint& /*touch*/,
                                                const char* last_event) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    if (sensor_label_) {
        char buf[80];
        if (imu.valid && rtc.valid) {
            std::snprintf(buf, sizeof(buf), "G(%.1f,%.1f,%.1f) %02d:%02d:%02d",
                          imu.ax, imu.ay, imu.az, rtc.hour, rtc.minute, rtc.second);
        } else if (imu.valid) {
            std::snprintf(buf, sizeof(buf), "IMU: %.2f, %.2f, %.2f g", imu.ax, imu.ay, imu.az);
        } else if (rtc.valid) {
            std::snprintf(buf, sizeof(buf), "RTC: %04d-%02d-%02d %02d:%02d:%02d",
                          rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
        } else {
            std::snprintf(buf, sizeof(buf), "Sensors: probing...");
        }
        lv_label_set_text(sensor_label_, buf);
    }
    if (event_label_ && last_event) {
        lv_label_set_text(event_label_, last_event);
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetBrightness(uint8_t percent) {
    brightness_percent_ = percent > 100 ? 100 : percent;
    if (initialized_) {
        bsp_display_brightness_set(brightness_percent_);
    }
}

void RoundAmoledDisplay::SetScreenOn(bool on) {
    if (!initialized_) {
        return;
    }
    if (on) {
        bsp_display_brightness_set(brightness_percent_ > 0 ? brightness_percent_ : 80);
    } else {
        bsp_display_backlight_off();
    }
}

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
