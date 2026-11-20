#include "ui/round_amoled/theme_renderer.h"

#if CONFIG_AS_ENABLE_LVGL

namespace agentsphere {

namespace {

lv_color_t StateRingColor(DeviceState state) {
    switch (state) {
        case DeviceState::Listening:       return lv_color_hex(0x00E5A0);  // active mic privacy ring
        case DeviceState::Thinking:        return lv_color_hex(0xFFB74D);
        case DeviceState::Speaking:        return lv_color_hex(0x5C9DFF);
        case DeviceState::WifiConfiguring: return lv_color_hex(0xFF8A65);
        case DeviceState::Activating:      return lv_color_hex(0xBA68C8);
        case DeviceState::Upgrading:       return lv_color_hex(0x4FC3F7);
        case DeviceState::Offline:         return lv_color_hex(0x607D8B);
        case DeviceState::Sleeping:        return lv_color_hex(0x1A1A1A);
        case DeviceState::Starting:
        case DeviceState::Idle:
        default:                           return lv_color_hex(0x26C6DA);
    }
}

}  // namespace

void ThemeRenderer::Create(lv_obj_t* parent) {
    // Circular edge halo ring aligned to 466x466 round AMOLED perimeter
    ring_ = lv_obj_create(parent);
    lv_obj_set_size(ring_, 458, 458);
    lv_obj_center(ring_);
    lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring_, 6, 0);
    lv_obj_set_style_border_color(ring_, StateRingColor(DeviceState::Starting), 0);
    lv_obj_set_style_border_opa(ring_, LV_OPA_80, 0);
    lv_obj_set_scrollable(ring_, false);
    lv_obj_set_clickable(ring_, false);

    // Left eye
    left_eye_ = lv_obj_create(parent);
    lv_obj_set_size(left_eye_, 54, 72);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -64, -76);
    lv_obj_set_style_radius(left_eye_, 26, 0);
    lv_obj_set_style_bg_color(left_eye_, lv_color_hex(0xE0F7FA), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_scrollable(left_eye_, false);
    lv_obj_set_clickable(left_eye_, false);

    // Right eye
    right_eye_ = lv_obj_create(parent);
    lv_obj_set_size(right_eye_, 54, 72);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, 64, -76);
    lv_obj_set_style_radius(right_eye_, 26, 0);
    lv_obj_set_style_bg_color(right_eye_, lv_color_hex(0xE0F7FA), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_scrollable(right_eye_, false);
    lv_obj_set_clickable(right_eye_, false);

    // Subtle mouth indicator
    mouth_ = lv_obj_create(parent);
    lv_obj_set_size(mouth_, 44, 10);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, -16);
    lv_obj_set_style_radius(mouth_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mouth_, lv_color_hex(0x80DEEA), 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mouth_, 0, 0);
    lv_obj_set_scrollable(mouth_, false);
    lv_obj_set_clickable(mouth_, false);
}

void ThemeRenderer::SetState(DeviceState state) {
    if (!ring_) {
        return;
    }
    lv_obj_set_style_border_color(ring_, StateRingColor(state), 0);
    lv_obj_set_style_border_width(ring_, state == DeviceState::Listening ? 10 : 6, 0);
}

void ThemeRenderer::SetEmotion(Emotion emotion) {
    if (!left_eye_ || !right_eye_ || !mouth_) {
        return;
    }

    int32_t eye_w = 54;
    int32_t eye_h = 72;
    int32_t mouth_w = 44;
    int32_t mouth_h = 10;
    lv_color_t eye_color = lv_color_hex(0xE0F7FA);

    switch (emotion) {
        case Emotion::Happy:
            eye_w = 64;
            eye_h = 28;
            mouth_w = 64;
            mouth_h = 16;
            eye_color = lv_color_hex(0x69F0AE);
            break;
        case Emotion::Surprised:
            eye_w = 66;
            eye_h = 84;
            mouth_w = 24;
            mouth_h = 24;
            eye_color = lv_color_hex(0x80D8FF);
            break;
        case Emotion::Thinking:
            eye_w = 52;
            eye_h = 46;
            mouth_w = 28;
            mouth_h = 8;
            eye_color = lv_color_hex(0xFFD54F);
            break;
        case Emotion::Dizzy:
            eye_w = 58;
            eye_h = 20;
            mouth_w = 32;
            mouth_h = 18;
            eye_color = lv_color_hex(0xFFAB91);
            break;
        case Emotion::Sleepy:
            eye_w = 56;
            eye_h = 14;
            mouth_w = 22;
            mouth_h = 6;
            eye_color = lv_color_hex(0x90A4AE);
            break;
        case Emotion::Charging:
            eye_w = 60;
            eye_h = 36;
            mouth_w = 54;
            mouth_h = 14;
            eye_color = lv_color_hex(0xA7FFEB);
            break;
        case Emotion::Confused:
            eye_w = 50;
            eye_h = 60;
            mouth_w = 30;
            mouth_h = 8;
            eye_color = lv_color_hex(0xCE93D8);
            break;
        case Emotion::Neutral:
        default:
            break;
    }

    lv_obj_set_size(left_eye_, eye_w, eye_h);
    lv_obj_set_size(right_eye_, eye_w, emotion == Emotion::Confused ? (eye_h - 18) : eye_h);
    lv_obj_set_style_bg_color(left_eye_, eye_color, 0);
    lv_obj_set_style_bg_color(right_eye_, eye_color, 0);
    lv_obj_set_size(mouth_, mouth_w, mouth_h);
}

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
