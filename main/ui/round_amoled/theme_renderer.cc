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
    // Compact top status indicator pill (avoids 466x466 full-screen circular mask overdraw on QSPI AMOLED)
    ring_ = lv_obj_create(parent);
    lv_obj_set_size(ring_, 96, 10);
    lv_obj_align(ring_, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring_, StateRingColor(DeviceState::Starting), 0);
    lv_obj_set_style_bg_opa(ring_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ring_, 0, 0);
    lv_obj_set_scrollable(ring_, false);
    lv_obj_set_clickable(ring_, false);

    avatar_.Create(parent);
}

void ThemeRenderer::SetState(DeviceState state) {
    avatar_.SetState(state);
    if (!ring_) {
        return;
    }
    lv_obj_set_style_bg_color(ring_, StateRingColor(state), 0);
    lv_obj_set_size(ring_, state == DeviceState::Listening ? 140 : 96,
                    state == DeviceState::Listening ? 14 : 10);
}

void ThemeRenderer::SetEmotion(Emotion emotion) {
    avatar_.SetEmotion(emotion);
}

void ThemeRenderer::SetScreenVisible(bool visible) {
    avatar_.SetScreenVisible(visible);
}

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
