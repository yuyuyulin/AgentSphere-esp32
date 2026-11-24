#include "ui/round_amoled/round_display.h"

#if CONFIG_AS_ENABLE_LVGL

#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "ui/fonts/fonts.h"
#include "ui/round_amoled/xiaoan_status.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "RoundDisplay";
constexpr uint32_t kLockTimeoutMs = 100;

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

    // Keep a common baseline below the avatar for prompts and future captions.
    status_label_ = lv_label_create(scr);
    lv_obj_set_width(status_label_, 276);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xE4EBF5), 0);
    lv_obj_set_style_text_line_space(status_label_, 4, 0);
    lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 346);

    low_battery_label_ = lv_label_create(scr);
    lv_label_set_text_static(low_battery_label_, "电量低，请充电");
    lv_obj_set_style_text_color(low_battery_label_, lv_color_hex(0xFFD49A), 0);
    lv_obj_align(low_battery_label_, LV_ALIGN_TOP_MID, 0, 74);
    UpdatePromptVisibility();

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

    self->theme_.SetEmotion(Emotion::Happy);

    AppEvent ev{};
    ev.type = AppEventType::Touch;
    ev.touch.pressed = true;
    ev.touch.x = static_cast<int16_t>(pt.x);
    ev.touch.y = static_cast<int16_t>(pt.y);
    xQueueSend(self->event_queue_, &ev, 0);
}

void RoundAmoledDisplay::UpdatePromptVisibility() {
    const bool show_prompts = !overlay_active_ && state_ != DeviceState::Sleeping;
    const char* status = XiaoAnStatusText(state_, emotion_);
    if (show_prompts && status) {
        lv_label_set_text_static(status_label_, status);
    }
    lv_obj_set_hidden(status_label_, !show_prompts || !status);
    lv_obj_set_hidden(low_battery_label_, !show_prompts || !low_battery_);
}

void RoundAmoledDisplay::SetState(DeviceState state, const char*) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    if (state_ != state) {
        state_ = state;
        emotion_ = Emotion::Neutral;
    }
    theme_.SetState(state);
    UpdatePromptVisibility();
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetEmotion(Emotion emotion) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    emotion_ = emotion;
    theme_.SetEmotion(emotion);
    UpdatePromptVisibility();
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetPowerStatus(const PowerStatus& power) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    const bool low_battery = XiaoAnShowLowBattery(power);
    if (low_battery_ != low_battery) {
        low_battery_ = low_battery;
        UpdatePromptVisibility();
    }
    // Retry a visibility update if a previous screen toggle could not lock LVGL.
    theme_.SetScreenVisible(requested_screen_on_.load());
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
    UpdatePromptVisibility();
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
    UpdatePromptVisibility();
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
    requested_screen_on_.store(on);
    if (bsp_display_lock(kLockTimeoutMs) == ESP_OK) {
        theme_.SetScreenVisible(requested_screen_on_.load());
        bsp_display_unlock();
    }
    if (on) {
        bsp_display_brightness_set(brightness_percent_ > 0 ? brightness_percent_ : 80);
    } else {
        bsp_display_backlight_off();
    }
}

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
