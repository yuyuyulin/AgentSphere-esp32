#include "ui/round_amoled/round_display.h"

#if CONFIG_AS_ENABLE_LVGL

#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "ui/fonts/fonts.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "RoundDisplay";
constexpr uint32_t kLockTimeoutMs = 100;

const char* DeviceStateToZh(DeviceState state) {
    switch (state) {
        case DeviceState::Starting:        return "启动中";
        case DeviceState::WifiConfiguring: return "配网中";
        case DeviceState::Activating:      return "配对中";
        case DeviceState::Upgrading:       return "升级中";
        case DeviceState::Idle:            return "待机中";
        case DeviceState::Listening:       return "聆听中";
        case DeviceState::Thinking:        return "思考中";
        case DeviceState::Speaking:        return "说话中";
        case DeviceState::Offline:         return "已离线";
        case DeviceState::Sleeping:        return "休眠中";
    }
    return "未知";
}

bool IsAsciiOnly(const char* s) {
    if (!s) return true;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        if (*p >= 0x80) return false;
    }
    return true;
}
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
    lv_obj_set_style_text_font(scr, &font_zh_18, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_add_event_cb(scr, &RoundAmoledDisplay::OnScreenPressed, LV_EVENT_PRESSED, this);

    theme_.Create(scr);

    status_label_ = lv_label_create(scr);
    lv_label_set_text(status_label_, "小安 | 正在启动");
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 22);

    wifi_label_ = lv_label_create(scr);
    lv_label_set_text(wifi_label_, "Wi-Fi: 连接中...");
    lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0x80D8FF), 0);
    lv_obj_align(wifi_label_, LV_ALIGN_CENTER, 0, 52);

    power_label_ = lv_label_create(scr);
    lv_label_set_text(power_label_, "电量: 检测中...");
    lv_obj_set_style_text_color(power_label_, lv_color_hex(0x80CBC4), 0);
    lv_obj_align(power_label_, LV_ALIGN_CENTER, 0, 82);

    sensor_label_ = lv_label_create(scr);
    lv_label_set_text(sensor_label_, "传感器: 检测中...");
    lv_obj_set_style_text_color(sensor_label_, lv_color_hex(0xB0BEC5), 0);
    lv_obj_align(sensor_label_, LV_ALIGN_CENTER, 0, 112);

    event_label_ = lv_label_create(scr);
    lv_label_set_text(event_label_, "轻触屏幕或按 BOOT 键");
    lv_obj_set_style_text_color(event_label_, lv_color_hex(0xFFE082), 0);
    lv_obj_align(event_label_, LV_ALIGN_CENTER, 0, 142);

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

    // Full-screen SoftAP Wi-Fi provisioning overlay (square 466x466 avoids circle mask overdraw)
    prov_panel_ = lv_obj_create(scr);
    lv_obj_set_size(prov_panel_, 466, 466);
    lv_obj_center(prov_panel_);
    lv_obj_set_style_radius(prov_panel_, 0, 0);
    lv_obj_set_style_bg_color(prov_panel_, lv_color_hex(0x0B1016), 0);
    lv_obj_set_style_bg_opa(prov_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(prov_panel_, 0, 0);
    lv_obj_set_style_pad_all(prov_panel_, 0, 0);
    lv_obj_set_scrollable(prov_panel_, false);

    lv_obj_t* prov_title = lv_label_create(prov_panel_);
    lv_label_set_text(prov_title, "Wi-Fi 配网 (2.4GHz)");
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
    lv_label_set_text(prov_ssid_label_, "热点: AgentSphere-XXXX");
    lv_obj_set_style_text_color(prov_ssid_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(prov_ssid_label_, LV_ALIGN_CENTER, 0, 74);

    prov_url_label_ = lv_label_create(prov_panel_);
    lv_label_set_text(prov_url_label_, "http://192.168.4.1");
    lv_obj_set_style_text_color(prov_url_label_, lv_color_hex(0x80D8FF), 0);
    lv_obj_align(prov_url_label_, LV_ALIGN_CENTER, 0, 104);

    lv_obj_t* skip_btn = lv_button_create(prov_panel_);
    lv_obj_set_size(skip_btn, 168, 48);
    lv_obj_align(skip_btn, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_set_style_radius(skip_btn, 24, 0);
    lv_obj_set_style_bg_color(skip_btn, lv_color_hex(0x263238), 0);
    lv_obj_add_event_cb(skip_btn, &RoundAmoledDisplay::OnSkipProvisioningClicked, LV_EVENT_PRESSED, this);

    lv_obj_t* skip_lbl = lv_label_create(skip_btn);
    lv_label_set_text(skip_lbl, "离线模式");
    lv_obj_center(skip_lbl);

    lv_obj_set_hidden(prov_panel_, true);

    // Full-screen 6-digit pairing overlay (square 466x466 avoids circle mask overdraw)
    pair_panel_ = lv_obj_create(scr);
    lv_obj_set_size(pair_panel_, 466, 466);
    lv_obj_center(pair_panel_);
    lv_obj_set_style_radius(pair_panel_, 0, 0);
    lv_obj_set_style_bg_color(pair_panel_, lv_color_hex(0x0B0E16), 0);
    lv_obj_set_style_bg_opa(pair_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pair_panel_, 0, 0);
    lv_obj_set_style_pad_all(pair_panel_, 0, 0);
    lv_obj_set_scrollable(pair_panel_, false);

    lv_obj_t* pair_title = lv_label_create(pair_panel_);
    lv_label_set_text(pair_title, "小安设备配对");
    lv_obj_set_style_text_color(pair_title, lv_color_hex(0xCE93D8), 0);
    lv_obj_align(pair_title, LV_ALIGN_TOP_MID, 0, 74);

    pair_code_label_ = lv_label_create(pair_panel_);
    lv_label_set_text(pair_code_label_, "------");
    lv_obj_set_style_text_color(pair_code_label_, lv_color_hex(0xFFFFFF), 0);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(pair_code_label_, &lv_font_montserrat_28, 0);
#endif
    lv_obj_set_style_text_letter_space(pair_code_label_, 6, 0);
    lv_obj_align(pair_code_label_, LV_ALIGN_CENTER, 0, -18);

    pair_hint_label_ = lv_label_create(pair_panel_);
    lv_label_set_text(pair_hint_label_, "请在网页端输入配对码");
    lv_obj_set_style_text_color(pair_hint_label_, lv_color_hex(0xB0BEC5), 0);
    lv_obj_align(pair_hint_label_, LV_ALIGN_CENTER, 0, 44);

    lv_obj_t* refresh_btn = lv_button_create(pair_panel_);
    lv_obj_set_size(refresh_btn, 210, 56);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, 0, -64);
    lv_obj_set_style_radius(refresh_btn, 28, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x6A1B9A), 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x9C27B0), LV_STATE_PRESSED);
    lv_obj_add_event_cb(refresh_btn, &RoundAmoledDisplay::OnRefreshPairingClicked, LV_EVENT_PRESSED, this);

    lv_obj_t* refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, "刷新配对码");
    lv_obj_center(refresh_lbl);

    lv_obj_set_hidden(pair_panel_, true);

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
        char buf[96];
        if (status_text && status_text[0] != '\0') {
            std::snprintf(buf, sizeof(buf), "%s | %s", DeviceStateToZh(state), status_text);
        } else {
            std::snprintf(buf, sizeof(buf), "%s", DeviceStateToZh(state));
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
    if (!initialized_ || overlay_active_ || !power_label_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    char buf[96];
    if (power.available) {
        std::snprintf(buf, sizeof(buf), "%s %d%% (%dmV)",
                      power.charging ? "充电中" : (power.usb_plugged ? "USB 供电" : "电池"),
                      power.battery_percent,
                      power.battery_mv);
    } else {
        std::snprintf(buf, sizeof(buf), "电量: 未知");
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
    char buf[96];
    if (connected) {
        std::snprintf(buf, sizeof(buf), "Wi-Fi: %s (%s)", ssid ? ssid : "", ip ? ip : "");
        lv_obj_set_style_text_color(wifi_label_, lv_color_hex(0x69F0AE), 0);
    } else {
        std::snprintf(buf, sizeof(buf), "未联网（长按 BOOT 配网）");
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
        char buf[96];
        std::snprintf(buf, sizeof(buf), "热点: %s", ap_ssid);
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
        overlay_active_ = true;
    } else {
        lv_obj_set_hidden(prov_panel_, true);
        overlay_active_ = (pair_panel_ && !lv_obj_is_hidden(pair_panel_));
    }
    lv_obj_invalidate(lv_screen_active());
    bsp_display_unlock();
}

void RoundAmoledDisplay::OnRefreshPairingClicked(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    if (!self || !self->event_queue_) {
        return;
    }
    const uint32_t now = lv_tick_get();
    static uint32_t last_press_ms = 0;
    if (now - last_press_ms < 1000) {
        return;
    }
    last_press_ms = now;

    ESP_LOGI(kTag, "Refresh Code button pressed");
    if (self->pair_code_label_) {
#if LV_FONT_MONTSERRAT_28
        lv_obj_set_style_text_font(self->pair_code_label_, &lv_font_montserrat_28, 0);
#endif
        lv_label_set_text(self->pair_code_label_, "......");
    }
    if (self->pair_hint_label_) {
        lv_label_set_text(self->pair_hint_label_, "正在获取新配对码...");
    }

    AppEvent ev{};
    ev.type = AppEventType::PairingRefreshRequested;
    xQueueSend(self->event_queue_, &ev, 0);
}

void RoundAmoledDisplay::ShowPairingCode(const char* code, const char* hint) {
    if (!initialized_ || !pair_panel_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    const bool was_hidden = lv_obj_is_hidden(pair_panel_);
    if (code && code[0] != '\0') {
        if (IsAsciiOnly(code)) {
#if LV_FONT_MONTSERRAT_28
            lv_obj_set_style_text_font(pair_code_label_, &lv_font_montserrat_28, 0);
#endif
            lv_obj_set_style_text_letter_space(pair_code_label_, 6, 0);
        } else {
            lv_obj_set_style_text_font(pair_code_label_, &font_zh_18, 0);
            lv_obj_set_style_text_letter_space(pair_code_label_, 2, 0);
        }
        lv_label_set_text(pair_code_label_, code);
        if (pair_hint_label_) {
            lv_label_set_text(pair_hint_label_,
                              (hint && hint[0] != '\0') ? hint : "请在网页端输入配对码");
        }
        lv_obj_set_hidden(pair_panel_, false);
        overlay_active_ = true;
        if (was_hidden) {
            lv_obj_invalidate(lv_screen_active());
        }
    } else {
        lv_obj_set_hidden(pair_panel_, true);
        overlay_active_ = (prov_panel_ && !lv_obj_is_hidden(prov_panel_));
        if (!was_hidden) {
            lv_obj_invalidate(lv_screen_active());
        }
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetDiagnosticTelemetry(const ImuData& imu,
                                                const RtcTime& rtc,
                                                const TouchPoint& /*touch*/,
                                                const char* last_event) {
    if (!initialized_ || overlay_active_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    if (sensor_label_) {
        char buf[96];
        if (imu.valid && rtc.valid) {
            std::snprintf(buf, sizeof(buf), "姿态(%.1f,%.1f,%.1f) %02d:%02d:%02d",
                          imu.ax, imu.ay, imu.az, rtc.hour, rtc.minute, rtc.second);
        } else if (imu.valid) {
            std::snprintf(buf, sizeof(buf), "姿态: %.2f, %.2f, %.2f g", imu.ax, imu.ay, imu.az);
        } else if (rtc.valid) {
            std::snprintf(buf, sizeof(buf), "时间: %04d-%02d-%02d %02d:%02d:%02d",
                          rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
        } else {
            std::snprintf(buf, sizeof(buf), "传感器: 检测中...");
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
