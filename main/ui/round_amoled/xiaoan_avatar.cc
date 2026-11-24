#include "ui/round_amoled/xiaoan_avatar.h"

#include <cstddef>

extern "C" const uint8_t xiaoan_sprite_data[] asm("_binary_xiaoan_sprites_rgb565_start");

namespace agentsphere {
namespace {

constexpr uint32_t kFrameWidth = 224;
constexpr uint32_t kFrameHeight = 188;
constexpr uint32_t kFrameStride = kFrameWidth * sizeof(uint16_t);
constexpr uint32_t kFrameBytes = kFrameStride * kFrameHeight;
constexpr int32_t kPortraitOffsetY = -12;

constexpr lv_image_dsc_t ImageFor(XiaoAnFrame frame) {
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RGB565;
    image.header.w = kFrameWidth;
    image.header.h = kFrameHeight;
    image.header.stride = kFrameStride;
    image.data_size = kFrameBytes;
    const auto index = static_cast<std::size_t>(frame) - 1;  // Hidden has no sprite.
    image.data = xiaoan_sprite_data + index * kFrameBytes;
    return image;
}

constexpr lv_image_dsc_t kImages[] = {
    ImageFor(XiaoAnFrame::Resting),
    ImageFor(XiaoAnFrame::Blink),
    ImageFor(XiaoAnFrame::Attentive),
    ImageFor(XiaoAnFrame::Thoughtful),
    ImageFor(XiaoAnFrame::Talking),
    ImageFor(XiaoAnFrame::Wave),
};

}  // namespace

void XiaoAnAvatar::Create(lv_obj_t* parent) {
    image_ = lv_image_create(parent);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    lv_obj_set_hidden(image_, true);
    lv_obj_align(image_, LV_ALIGN_CENTER, 0, kPortraitOffsetY);
    lv_obj_add_event_cb(image_, &XiaoAnAvatar::OnImageDeleted, LV_EVENT_DELETE, this);
    timer_ = lv_timer_create(&XiaoAnAvatar::OnTimer, 1, this);
    RestartPose();
}

void XiaoAnAvatar::SetState(DeviceState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emotion_ = Emotion::Neutral;
    RestartPose();
}

void XiaoAnAvatar::SetEmotion(Emotion emotion) {
    if (!screen_visible_) {
        return;
    }
    if (state_ == DeviceState::Starting) {
        emotion_ = emotion;
        Render();  // Let the startup greeting finish at its original deadline.
        return;
    }
    if (state_ != DeviceState::Idle && state_ != DeviceState::Offline) {
        return;
    }
    if (emotion_ == Emotion::Neutral && emotion == Emotion::Neutral) {
        return;
    }
    emotion_ = emotion;
    RestartPose();
}

void XiaoAnAvatar::SetScreenVisible(bool visible) {
    if (screen_visible_ == visible) {
        return;
    }
    screen_visible_ = visible;
    emotion_ = Emotion::Neutral;
    RestartPose();
}

void XiaoAnAvatar::OnTimer(lv_timer_t* timer) {
    static_cast<XiaoAnAvatar*>(lv_timer_get_user_data(timer))->Render();
}

void XiaoAnAvatar::OnImageDeleted(lv_event_t* event) {
    auto* self = static_cast<XiaoAnAvatar*>(lv_event_get_user_data(event));
    if (self->timer_) {
        lv_timer_delete(self->timer_);
        self->timer_ = nullptr;
    }
    self->image_ = nullptr;
    self->frame_ = XiaoAnFrame::Hidden;
    self->offset_y_ = 0;
}

void XiaoAnAvatar::RestartPose() {
    pose_started_at_ = lv_tick_get();
    Render();
}

void XiaoAnAvatar::Render() {
    if (!image_) {
        return;
    }
    const auto pose = GetXiaoAnPose(state_, emotion_, screen_visible_, lv_tick_elaps(pose_started_at_));
    if (frame_ != pose.frame) {
        frame_ = pose.frame;
        const bool hidden = frame_ == XiaoAnFrame::Hidden;
        if (!hidden) {
            lv_image_set_src(image_, &kImages[static_cast<std::size_t>(frame_) - 1]);
        }
        lv_obj_set_hidden(image_, hidden);
    }

    if (offset_y_ != pose.offset_y) {
        offset_y_ = pose.offset_y;
        lv_obj_set_y(image_, kPortraitOffsetY + offset_y_);
    }

    if (timer_) {
        if (pose.next_update_ms == 0) {
            lv_timer_pause(timer_);
        } else {
            lv_timer_set_period(timer_, pose.next_update_ms);
            lv_timer_reset(timer_);
            lv_timer_resume(timer_);
        }
    }
}

}  // namespace agentsphere
