#include "ui/round_amoled/round_display.h"

#if CONFIG_AS_ENABLE_LVGL

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "ui/fonts/fonts.h"
#include "ui/round_amoled/quick_panel_gesture.h"
#include "ui/round_amoled/xiaoan_status.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "RoundDisplay";
constexpr uint32_t kLockTimeoutMs = 100;
constexpr uint32_t kCaptionScrollIntervalMs = 100;
constexpr uint32_t kCaptionScrollDelayMs = 1500;
constexpr int32_t kCaptionScrollStep = 2;

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

    if (bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to lock LVGL display");
        return ESP_ERR_TIMEOUT;
    }
    ApplyBrightnessLocked();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, &font_zh_18, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_press_lock(scr, true);
    lv_obj_add_event_cb(scr, &RoundAmoledDisplay::OnHomeTouch, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(scr, &RoundAmoledDisplay::OnHomeTouch, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(scr, &RoundAmoledDisplay::OnHomeTouch, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(scr, &RoundAmoledDisplay::OnHomeTouch, LV_EVENT_PRESS_LOST, this);

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
    CreateQuickPanel(scr);
    CreatePracticePanel(scr);

    bsp_display_unlock();

    initialized_ = true;
    ESP_LOGI(kTag, "466x466 Round AMOLED display initialized (brightness=%u%%)", brightness_percent_);
    return ESP_OK;
}

void RoundAmoledDisplay::CreateQuickPanel(lv_obj_t* parent) {
    quick_panel_ = lv_obj_create(parent);
    lv_obj_set_size(quick_panel_, 466, 466);
    lv_obj_center(quick_panel_);
    lv_obj_set_style_radius(quick_panel_, 0, 0);
    lv_obj_set_style_border_width(quick_panel_, 0, 0);
    lv_obj_set_style_pad_all(quick_panel_, 0, 0);
    lv_obj_set_style_bg_color(quick_panel_, lv_color_hex(0x121A1D), 0);
    lv_obj_set_style_bg_opa(quick_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(quick_panel_, lv_color_hex(0xEDF5F3), 0);
    lv_obj_set_scrollable(quick_panel_, false);
    lv_obj_set_gesture_bubble(quick_panel_, false);
    lv_obj_add_event_cb(quick_panel_, &RoundAmoledDisplay::OnQuickPanelGesture, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(quick_panel_, &RoundAmoledDisplay::OnQuickPanelGesture, LV_EVENT_PRESSED, this);

    auto label = [this](const char* text, int x, int y) {
        auto* obj = lv_label_create(quick_panel_);
        lv_label_set_text_static(obj, text);
        lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
        return obj;
    };
    auto status_icon = [&](const char* icon, int x) {
        auto* object = label(icon, x, -155);
        lv_obj_set_style_text_font(object, LV_FONT_DEFAULT, 0);
        return object;
    };
    status_icon(LV_SYMBOL_WIFI, -106);
    quick_wifi_ = label("离线", -67, -155);
    quick_battery_icon_ = status_icon(LV_SYMBOL_BATTERY_EMPTY, 38);
    quick_battery_ = label("--", 87, -155);
    quick_charge_status_ = label("", 75, -128);
    lv_obj_set_style_text_color(quick_charge_status_, lv_color_hex(0xA9C4BF), 0);

    auto make_slider = [this](int min, int y) {
        auto* slider = lv_slider_create(quick_panel_);
        lv_slider_set_range(slider, min, 100);
        lv_obj_set_size(slider, 230, 18);
        lv_obj_set_ext_click_area(slider, 16);
        lv_obj_align(slider, LV_ALIGN_CENTER, 0, y);
        lv_obj_set_press_lock(slider, true);
        lv_obj_set_gesture_bubble(slider, false);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x6AD5AC), LV_PART_INDICATOR);
        lv_obj_add_event_cb(slider, &RoundAmoledDisplay::OnQuickControl, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(slider, &RoundAmoledDisplay::OnQuickControl, LV_EVENT_RELEASED, this);
        return slider;
    };
    label("音量", 0, -110);
    quick_volume_ = make_slider(0, -74);
    label("亮度", 0, -24);
    quick_brightness_ = make_slider(QuickControls::kMinBrightness, 12);
    lv_obj_add_event_cb(quick_brightness_, &RoundAmoledDisplay::OnQuickControl, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(quick_brightness_, &RoundAmoledDisplay::OnQuickControl, LV_EVENT_PRESS_LOST, this);
    label("闭麦", -52, 83);
    quick_mic_ = lv_switch_create(quick_panel_);
    lv_obj_set_size(quick_mic_, 67, 40);
    lv_obj_set_ext_click_area(quick_mic_, 8);
    lv_obj_align(quick_mic_, LV_ALIGN_CENTER, 39, 83);
    lv_obj_set_gesture_bubble(quick_mic_, false);
    lv_obj_set_style_bg_color(quick_mic_, lv_color_hex(0x6AD5AC), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    lv_obj_add_event_cb(quick_mic_, &RoundAmoledDisplay::OnQuickControl, LV_EVENT_VALUE_CHANGED, this);

    auto* back = lv_button_create(quick_panel_);
    lv_obj_set_size(back, 112, 46);
    lv_obj_align(back, LV_ALIGN_CENTER, 0, 151);
    lv_obj_set_style_radius(back, 23, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x273B3E), 0);
    auto* back_label = lv_label_create(back);
    lv_label_set_text_static(back_label, "返回");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back, &RoundAmoledDisplay::OnQuickPanelGesture, LV_EVENT_CLICKED, this);

    lv_obj_set_hidden(quick_panel_, true);
}

void RoundAmoledDisplay::PostQuickEvent(AppEventType type, int value) {
    if (!event_queue_) return;
    AppEvent ev{};
    ev.type = type;
    ev.value = value;
    xQueueSend(event_queue_, &ev, 0);
}

void RoundAmoledDisplay::OnHomeTouch(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    auto* indev = lv_indev_active();
    const auto code = lv_event_get_code(e);
    if (!indev || code == LV_EVENT_PRESS_LOST || !self->requested_screen_on_.load() ||
        self->overlay_active_ || self->quick_visible_ || !lv_obj_is_hidden(self->practice_panel_)) {
        self->home_press_active_ = false;
        return;
    }

    lv_point_t pt{};
    lv_indev_get_point(indev, &pt);
    if (code == LV_EVENT_PRESSED) {
        self->home_press_ = pt;
        self->home_press_active_ = true;
        self->PostQuickEvent(AppEventType::DisplayActivity);
        return;
    }
    if (!self->home_press_active_) return;

    const auto start = self->home_press_;
    if (OpensQuickPanel(start.x, start.y, pt.x, pt.y)) {
        self->home_press_active_ = false;
        // Keep the rest of this swipe away from the panel's sliders and switch.
        if (code == LV_EVENT_PRESSING) lv_indev_wait_release(indev);
        self->SetQuickPanelVisible(true);
        self->PostQuickEvent(AppEventType::DisplayActivity);
        ESP_LOGI(kTag, "Quick controls opened by downward swipe");
        return;
    }

    if (OpensEnglishMenu(start.x, start.y, pt.x, pt.y)) {
        self->home_press_active_ = false;
        if (code == LV_EVENT_PRESSING) lv_indev_wait_release(indev);
        self->practice_menu_ = true;
        self->RefreshVisibility();
        self->PostQuickEvent(AppEventType::DisplayActivity);
        lv_mem_monitor_t mon{};
        lv_mem_monitor(&mon);
        ESP_LOGI(kTag, "English practice menu opened by left swipe (lv_mem free=%u/%u used=%u%%)",
                 static_cast<unsigned>(mon.free_size),
                 static_cast<unsigned>(mon.total_size),
                 static_cast<unsigned>(mon.used_pct));
        return;
    }

    if (code != LV_EVENT_RELEASED) return;
    self->home_press_active_ = false;
    if (!self->event_queue_ || std::abs(pt.x - start.x) >= 24 || std::abs(pt.y - start.y) >= 24) return;
    self->theme_.SetEmotion(Emotion::Happy);
    AppEvent ev{};
    ev.type = AppEventType::Touch;
    ev.touch = {true, static_cast<int16_t>(pt.x), static_cast<int16_t>(pt.y)};
    xQueueSend(self->event_queue_, &ev, 0);
}

void RoundAmoledDisplay::OnQuickPanelGesture(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    const auto code = lv_event_get_code(e);
    auto* indev = lv_indev_active();
    if (code == LV_EVENT_CLICKED ||
        (code == LV_EVENT_GESTURE && indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP)) {
        self->SetQuickPanelVisible(false);
    }
    self->PostQuickEvent(AppEventType::DisplayActivity);
}

void RoundAmoledDisplay::OnQuickControl(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        self->PostQuickEvent(AppEventType::DisplayActivity);
    } else if (target == self->quick_mic_) {
        self->PostQuickEvent(AppEventType::MicrophoneMuteChanged, lv_obj_has_state(target, LV_STATE_CHECKED));
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (self->quick_visible_ && self->requested_screen_on_.load()) {
            self->requested_brightness_.store(QuickControls::ClampBrightness(lv_slider_get_value(target)));
            self->ApplyBrightnessLocked();
        }
    } else {
        const auto type = target == self->quick_volume_ ? AppEventType::VolumeChanged : AppEventType::BrightnessChanged;
        self->PostQuickEvent(type, lv_slider_get_value(target));
    }
}

void RoundAmoledDisplay::SetQuickPanelVisible(bool visible) {
    if (quick_visible_ == visible) return;
    quick_visible_ = visible;
    if (visible) UpdateQuickStatus();
    lv_obj_set_hidden(quick_panel_, !visible);
    RefreshVisibility();
}

void RoundAmoledDisplay::CreatePracticePanel(lv_obj_t* parent) {
    practice_panel_ = lv_obj_create(parent);
    lv_obj_set_size(practice_panel_, 466, 466);
    lv_obj_center(practice_panel_);
    lv_obj_set_style_radius(practice_panel_, 0, 0);
    lv_obj_set_style_border_width(practice_panel_, 0, 0);
    lv_obj_set_style_pad_all(practice_panel_, 0, 0);
    lv_obj_set_style_bg_color(practice_panel_, lv_color_hex(0x0D191D), 0);
    lv_obj_set_style_bg_opa(practice_panel_, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(practice_panel_, false);

    auto label = [this](const char* text, int y) {
        auto* obj = lv_label_create(practice_panel_);
        lv_label_set_text_static(obj, text);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xE4F3EE), 0);
        lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
        return obj;
    };
    label("英语练习", 56);
    practice_status_ = label("", 248);
    lv_obj_set_width(practice_status_, 312);
    lv_obj_set_style_text_line_space(practice_status_, 4, 0);
    practice_caption_view_ = lv_obj_create(practice_panel_);
    lv_obj_remove_style_all(practice_caption_view_);
    lv_obj_set_size(practice_caption_view_, 310, 178);
    lv_obj_align(practice_caption_view_, LV_ALIGN_TOP_MID, 0, 151);
    lv_obj_set_scrollable(practice_caption_view_, true);
    lv_obj_set_scroll_dir(practice_caption_view_, LV_DIR_VER);
    lv_obj_set_scroll_chain_hor(practice_caption_view_, false);
    lv_obj_set_scroll_chain_ver(practice_caption_view_, false);
    lv_obj_set_scroll_elastic(practice_caption_view_, false);
    lv_obj_set_scrollbar_mode(practice_caption_view_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(practice_caption_view_, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(practice_caption_view_, lv_color_hex(0x79B4A5), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(practice_caption_view_, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(practice_caption_view_, &RoundAmoledDisplay::OnCaptionScroll,
                        LV_EVENT_SCROLL_BEGIN, this);

    practice_caption_ = lv_label_create(practice_caption_view_);
    lv_obj_set_width(practice_caption_, 284);
    lv_obj_set_style_text_font(practice_caption_, &font_practice_zh_16, 0);
    lv_obj_set_style_text_color(practice_caption_, lv_color_hex(0xE4F3EE), 0);
    lv_obj_set_style_text_line_space(practice_caption_, 4, 0);
    lv_obj_align(practice_caption_, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text_static(practice_caption_, "");
    caption_timer_ = lv_timer_create(&RoundAmoledDisplay::OnCaptionTimer, kCaptionScrollIntervalMs, this);
    if (caption_timer_) lv_timer_pause(caption_timer_);

    // Prominent English learning icon card ("ABC" + "开始练习") shown on left swipe.
    // Disable default LVGL button shadows to keep CO5300 partial flushes crisp and avoid extra shadow buffers.
    practice_enter_ = lv_button_create(practice_panel_);
    lv_obj_set_size(practice_enter_, 160, 116);
    lv_obj_align(practice_enter_, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_radius(practice_enter_, 24, 0);
    lv_obj_set_style_pad_all(practice_enter_, 0, 0);
    lv_obj_set_style_shadow_width(practice_enter_, 0, 0);
    lv_obj_set_style_outline_width(practice_enter_, 0, 0);
    lv_obj_set_style_bg_opa(practice_enter_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(practice_enter_, lv_color_hex(0x184742), 0);
    lv_obj_set_style_bg_color(practice_enter_, lv_color_hex(0x25655D), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(practice_enter_, 2, 0);
    lv_obj_set_style_border_color(practice_enter_, lv_color_hex(0x6AD5AC), 0);
    lv_obj_add_event_cb(practice_enter_, &RoundAmoledDisplay::OnPracticeAction, LV_EVENT_CLICKED, this);

    auto* icon_abc = lv_label_create(practice_enter_);
    lv_label_set_text_static(icon_abc, "ABC");
    lv_obj_set_style_text_color(icon_abc, lv_color_hex(0xFFFFFF), 0);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(icon_abc, &lv_font_montserrat_28, 0);
#endif
    lv_obj_set_style_text_letter_space(icon_abc, 4, 0);
    lv_obj_align(icon_abc, LV_ALIGN_CENTER, 0, -16);

    auto* enter_text = lv_label_create(practice_enter_);
    lv_label_set_text_static(enter_text, "开始练习");
    lv_obj_set_style_text_color(enter_text, lv_color_hex(0xD6F5EB), 0);
    lv_obj_align(enter_text, LV_ALIGN_CENTER, 0, 26);

    auto button = [this](const char* name, int width, int x, lv_obj_t** out_label = nullptr) {
        auto* obj = lv_button_create(practice_panel_);
        lv_obj_set_size(obj, width, 48);
        lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, x, -64);
        lv_obj_set_style_radius(obj, 24, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        lv_obj_set_style_outline_width(obj, 0, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x254C46), 0);
        lv_obj_add_event_cb(obj, &RoundAmoledDisplay::OnPracticeAction, LV_EVENT_CLICKED, this);
        auto* text = lv_label_create(obj);
        lv_label_set_text_static(text, name);
        lv_obj_center(text);
        if (out_label) *out_label = text;
        return obj;
    };
    practice_repeat_ = button("重听", 116, -68);
    practice_translate_ = button("翻译", 86, 0, &practice_translate_label_);
    practice_back_ = button("返回", 116, 0, &practice_back_label_);
    lv_obj_set_hidden(practice_panel_, true);
}

void RoundAmoledDisplay::OnPracticeAction(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    auto* target = lv_event_get_target(e);
    if (target == self->practice_enter_) {
        ESP_LOGI(kTag, "English practice icon tapped");
        self->PostQuickEvent(AppEventType::PracticeEnter);
    } else if (target == self->practice_repeat_) {
        self->PostQuickEvent(AppEventType::PracticeRepeat);
    } else if (target == self->practice_translate_) {
        self->caption_translation_visible_ = !self->caption_translation_visible_;
        self->UpdatePracticeCaption(false);
        self->PostQuickEvent(AppEventType::DisplayActivity);
    } else {
        self->practice_menu_ = false;
        self->RefreshVisibility();
        self->PostQuickEvent(AppEventType::PracticeExit);
    }
}

void RoundAmoledDisplay::OnCaptionScroll(lv_event_t* e) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_event_get_user_data(e));
    auto* indev = lv_indev_active();
    if (indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
        lv_indev_get_scroll_obj(indev) == self->practice_caption_view_) {
        self->caption_manual_scroll_ = true;
        self->UpdateCaptionTimer();
        self->PostQuickEvent(AppEventType::DisplayActivity);
    }
}

void RoundAmoledDisplay::OnCaptionTimer(lv_timer_t* timer) {
    auto* self = static_cast<RoundAmoledDisplay*>(lv_timer_get_user_data(timer));
    if (lv_tick_elaps(self->caption_start_tick_) < kCaptionScrollDelayMs) return;
    auto* view = self->practice_caption_view_;
    const int32_t remaining = lv_obj_get_scroll_bottom(view);
    if (remaining <= 0) {
        lv_timer_pause(timer);
        return;
    }
    lv_obj_scroll_by(view, 0, -std::min(kCaptionScrollStep, remaining), LV_ANIM_OFF);
}

void RoundAmoledDisplay::UpdateCaptionTimer() {
    if (!caption_timer_) return;
    if (practice_shown_ && practice_.page == PracticePage::Active && !practice_.caption.empty() &&
        !caption_manual_scroll_) {
        lv_timer_resume(caption_timer_);
    } else {
        lv_timer_pause(caption_timer_);
    }
}

void RoundAmoledDisplay::UpdatePracticeCaption(bool reset_scroll) {
    const int32_t previous_scroll = reset_scroll ? 0 : lv_obj_get_scroll_y(practice_caption_view_);
    if (reset_scroll) {
        caption_translation_visible_ = false;
        caption_manual_scroll_ = false;
    }
    caption_start_tick_ = lv_tick_get();
    if (caption_translation_visible_) {
        const auto text = practice_.caption + "\n\n" + practice_.caption_zh;
        lv_label_set_text(practice_caption_, text.c_str());
    } else {
        lv_label_set_text(practice_caption_, practice_.caption.c_str());
    }
    lv_label_set_text_static(practice_translate_label_, caption_translation_visible_ ? "收起" : "翻译");
    lv_obj_update_layout(practice_caption_view_);
    lv_obj_scroll_to_y(practice_caption_view_, previous_scroll, LV_ANIM_OFF);
    UpdateCaptionTimer();
}

const char* RoundAmoledDisplay::PracticeStatus() const {
    if (practice_.page == PracticePage::Offline) return "联网后再试";
    if (practice_.page == PracticePage::Checking) return "正在检查...";
    if (practice_.page == PracticePage::Unavailable) return "未开启，请先在网页端开启";
    if (practice_.page == PracticePage::Entering) return "正在进入...";
    if (practice_.page != PracticePage::Active) return "";
    switch (state_) {
        case DeviceState::Listening: return "正在聆听...";
        case DeviceState::Thinking: return "正在思考...";
        case DeviceState::Speaking: return "正在播报...";
        default:
            if (emotion_ == Emotion::Confused) return "对话失败，请重试";
            return muted_ ? "已闭麦" : "按 BOOT 回答";
    }
}

void RoundAmoledDisplay::RefreshVisibility() {
    const bool on = requested_screen_on_.load();
    const bool busy_or_active = practice_.page == PracticePage::Checking ||
                                practice_.page == PracticePage::Entering ||
                                practice_.page == PracticePage::Active;
    const bool show = on && !overlay_active_ && !quick_visible_ && (practice_menu_ || busy_or_active);
    const bool in_menu = !busy_or_active;
    const bool is_active = practice_.page == PracticePage::Active;
    const bool can_translate = is_active && !practice_.caption.empty() && !practice_.caption_zh.empty();
    const bool layout_changed = (show != practice_shown_) ||
                                (show && (in_menu != practice_in_menu_ || is_active != practice_active_layout_ ||
                                          can_translate != practice_translation_layout_));
    practice_shown_ = show;
    if (show) {
        practice_in_menu_ = in_menu;
        practice_active_layout_ = is_active;
        practice_translation_layout_ = can_translate;
    }

    lv_obj_set_hidden(practice_panel_, !show);
    if (show) {
        const char* status = PracticeStatus();
        if (std::strcmp(lv_label_get_text(practice_status_), status) != 0) {
            lv_label_set_text_static(practice_status_, status);
        }
        if (layout_changed) {
            lv_obj_align(practice_status_, LV_ALIGN_TOP_MID, 0, in_menu ? 248 : 106);
        }
        const lv_color_t status_color = lv_color_hex(
            practice_.page == PracticePage::Unavailable || practice_.page == PracticePage::Offline
                ? 0xFFD49A
                : 0xE4F3EE);
        if (!lv_color_eq(lv_obj_get_style_text_color(practice_status_, LV_PART_MAIN), status_color)) {
            lv_obj_set_style_text_color(practice_status_, status_color, 0);
        }
        lv_obj_set_hidden(practice_caption_view_, !is_active || practice_.caption.empty());
        lv_obj_set_hidden(practice_translate_, !can_translate);
        lv_obj_set_hidden(practice_enter_, !in_menu);
        lv_obj_set_hidden(practice_repeat_, practice_.page != PracticePage::Active);
        lv_obj_set_state(practice_repeat_, LV_STATE_DISABLED, !practice_.can_repeat);
        if (practice_back_label_) {
            const char* back_text = in_menu ? "返回" : "退出";
            if (std::strcmp(lv_label_get_text(practice_back_label_), back_text) != 0) {
                lv_label_set_text_static(practice_back_label_, back_text);
            }
        }
        if (layout_changed) {
            const int width = can_translate ? 86 : 116;
            lv_obj_set_width(practice_repeat_, width);
            lv_obj_set_width(practice_back_, width);
            const int offset = can_translate ? 97 : 68;
            lv_obj_align(practice_repeat_, LV_ALIGN_BOTTOM_MID, -offset, -64);
            lv_obj_align(practice_back_, LV_ALIGN_BOTTOM_MID, is_active ? offset : 0, -64);
        }
    }
    if (layout_changed) {
        lv_obj_invalidate(lv_screen_active());
        UpdateCaptionTimer();
    }
    theme_.SetScreenVisible(on && !quick_visible_ && !show);
    UpdatePromptVisibility();
}

void RoundAmoledDisplay::SetPractice(const PracticeView& practice) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) return;
    if (!(practice_ == practice)) {
        if (practice_.page == PracticePage::Active && practice.page != PracticePage::Active) {
            practice_menu_ = false;
        }
        const bool caption_changed = !practice_.SameCaption(practice);
        practice_ = practice;
        RefreshVisibility();
        if (caption_changed) UpdatePracticeCaption(true);
    }
    bsp_display_unlock();
}

void RoundAmoledDisplay::UpdateQuickStatus() {
    lv_label_set_text_static(quick_wifi_, wifi_connected_ ? "在线" : "离线");
    constexpr const char* kBatteryIcons[] = {
        LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1, LV_SYMBOL_BATTERY_2,
        LV_SYMBOL_BATTERY_3, LV_SYMBOL_BATTERY_FULL,
    };
    const int level = XiaoAnBatteryIconLevel(power_);
    lv_obj_set_hidden(quick_battery_icon_, level < 0);
    if (level >= 0) {
        lv_label_set_text_static(quick_battery_icon_, kBatteryIcons[level]);
        lv_label_set_text_fmt(quick_battery_, "%d%%", power_.battery_percent);
    } else {
        lv_label_set_text_static(quick_battery_, "--");
    }
    const char* charge_status = XiaoAnPanelPowerText(power_);
    if (charge_status) lv_label_set_text_static(quick_charge_status_, charge_status);
    lv_obj_set_hidden(quick_charge_status_, charge_status == nullptr);
}

void RoundAmoledDisplay::SetQuickControls(const QuickControls& controls) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) return;
    muted_ = controls.muted;
    lv_slider_set_value(quick_volume_, controls.volume, LV_ANIM_OFF);
    lv_slider_set_value(quick_brightness_, controls.brightness, LV_ANIM_OFF);
    lv_obj_set_state(quick_mic_, LV_STATE_CHECKED, controls.muted);
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetWifiStatus(bool connected, const char*, const char*) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) return;
    wifi_connected_ = connected;
    if (quick_visible_) UpdateQuickStatus();
    bsp_display_unlock();
}

void RoundAmoledDisplay::UpdatePromptVisibility() {
    const bool show_prompts = requested_screen_on_.load() && !overlay_active_ && !quick_visible_ &&
                              (!practice_panel_ || lv_obj_is_hidden(practice_panel_)) &&
                              state_ != DeviceState::Sleeping;
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
    if (practice_.page == PracticePage::Available && state != DeviceState::Idle) practice_menu_ = false;
    theme_.SetState(state);
    RefreshVisibility();
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetEmotion(Emotion emotion) {
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) {
        return;
    }
    emotion_ = emotion;
    theme_.SetEmotion(emotion);
    RefreshVisibility();
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
    const bool power_changed =
        power_.battery_percent != power.battery_percent || power_.available != power.available ||
        power_.usb_plugged != power.usb_plugged || power_.battery_present != power.battery_present ||
        power_.charging != power.charging || power_.charge_complete != power.charge_complete;
    power_ = power;
    if (power_changed && quick_visible_) UpdateQuickStatus();
    // Retry pending screen changes if an earlier call could not lock LVGL.
    ApplyBrightnessLocked();
    if (!requested_screen_on_.load()) {
        practice_menu_ = false;
        SetQuickPanelVisible(false);
    }
    RefreshVisibility();
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
        practice_menu_ = false;
        SetQuickPanelVisible(false);
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
    RefreshVisibility();
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
        practice_menu_ = false;
        SetQuickPanelVisible(false);
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
    RefreshVisibility();
    bsp_display_unlock();
}

void RoundAmoledDisplay::ApplyBrightnessLocked() {
    // LVGL flush and AMOLED brightness both use the same SPI device.
    const uint8_t level = requested_screen_on_.load() ? requested_brightness_.load() : 0;
    if (level == applied_brightness_) return;
    const auto err = level == 0 ? bsp_display_backlight_off() : bsp_display_brightness_set(level);
    if (err == ESP_OK) applied_brightness_ = level;
}

void RoundAmoledDisplay::SetBrightness(uint8_t percent) {
    brightness_percent_ = QuickControls::ClampBrightness(percent);
    requested_brightness_.store(brightness_percent_);
    if (!initialized_ || bsp_display_lock(kLockTimeoutMs) != ESP_OK) return;
    ApplyBrightnessLocked();
    bsp_display_unlock();
}

void RoundAmoledDisplay::SetScreenOn(bool on) {
    if (!initialized_) {
        return;
    }
    if (on) requested_brightness_.store(brightness_percent_);
    requested_screen_on_.store(on);
    if (bsp_display_lock(kLockTimeoutMs) == ESP_OK) {
        if (!on) {
            practice_menu_ = false;
            SetQuickPanelVisible(false);
        }
        RefreshVisibility();
        ApplyBrightnessLocked();
        bsp_display_unlock();
    }
}

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
