#include "ui/round_amoled/theme_renderer.h"

#if CONFIG_AS_ENABLE_LVGL

namespace agentsphere {

void ThemeRenderer::Create(lv_obj_t* parent) {
    // The microphone indicator is visible while the device is listening.
    ring_ = lv_obj_create(parent);
    lv_obj_set_size(ring_, 96, 8);
    lv_obj_align(ring_, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring_, lv_color_hex(0x00E5A0), 0);
    lv_obj_set_style_bg_opa(ring_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ring_, 0, 0);
    lv_obj_set_scrollable(ring_, false);
    lv_obj_set_clickable(ring_, false);
    lv_obj_set_hidden(ring_, true);

    avatar_.Create(parent);
}

void ThemeRenderer::SetState(DeviceState state) {
    avatar_.SetState(state);
    if (!ring_) {
        return;
    }
    lv_obj_set_hidden(ring_, state != DeviceState::Listening);
}

void ThemeRenderer::SetEmotion(Emotion emotion) {
    avatar_.SetEmotion(emotion);
}

void ThemeRenderer::SetScreenVisible(bool visible) {
    avatar_.SetScreenVisible(visible);
}

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
