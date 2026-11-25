#include "ui/round_amoled/xiaoan_avatar.h"

#include <cmath>

#include "ui/round_amoled/xiaoan_rig_assets.h"

extern "C" const uint8_t xiaoan_rig_data[] asm("_binary_xiaoan_rig_rgb565a8_start");

namespace agentsphere {
namespace {

using namespace xiaoan_assets;

constexpr lv_point_t kHeadPosition{34, 3};
constexpr lv_point_t kHeadPivot{93, 139};
constexpr lv_point_t kTorsoPosition{66, 129};
constexpr lv_point_t kLeftArmPosition{50, 136};
constexpr lv_point_t kRightArmPosition{158, 135};
constexpr lv_point_t kRightArmPivot{12, 13};
constexpr lv_point_t kEyesCenter{130, 100};
constexpr lv_point_t kMouthCenter{139, 123};

constexpr lv_image_dsc_t ImageFor(RigImage part) {
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RGB565A8;
    image.header.w = part.width;
    image.header.h = part.height;
    image.header.stride = part.width * sizeof(uint16_t);
    image.data_size = part.width * part.height * 3U;
    image.data = xiaoan_rig_data + part.offset;
    return image;
}

constexpr auto kHeadImage = ImageFor(kHead);
constexpr auto kTorsoImage = ImageFor(kTorso);
constexpr auto kLeftArmImage = ImageFor(kLeftArm);
constexpr auto kRightArmImage = ImageFor(kRightArm);
constexpr lv_image_dsc_t kEyeImages[] = {ImageFor(kEyesOpen), ImageFor(kEyesClosed)};
constexpr lv_image_dsc_t kMouthImages[] = {ImageFor(kSmile), ImageFor(kSpeaking), ImageFor(kThinking)};

lv_obj_t* CreatePart(lv_obj_t* root, const lv_image_dsc_t& image, lv_point_t position) {
    auto* object = lv_image_create(root);
    lv_image_set_src(object, &image);
    lv_obj_set_pos(object, position.x, position.y);
    lv_obj_set_clickable(object, false);
    lv_obj_set_scrollable(object, false);
    return object;
}

// Eyes and mouth are separate draw objects with the same head-space anchors.
// Rotating their anchors keeps the features attached without redrawing a canvas.
void PlaceFacePart(lv_obj_t* object, RigImage image, lv_point_t center,
                   float sine, float cosine, int16_t angle) {
    constexpr int32_t kPivotX = kHeadPosition.x + kHeadPivot.x;
    constexpr int32_t kPivotY = kHeadPosition.y + kHeadPivot.y;
    const int32_t x = center.x - kPivotX;
    const int32_t y = center.y - kPivotY;
    const auto rx = static_cast<int32_t>(std::lround(x * cosine - y * sine));
    const auto ry = static_cast<int32_t>(std::lround(x * sine + y * cosine));
    lv_obj_set_pos(object, kPivotX + rx - image.width / 2, kPivotY + ry - image.height / 2);
    lv_image_set_rotation(object, angle);
}

}  // namespace

void XiaoAnAvatar::Create(lv_obj_t* parent) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 260, 210);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_clickable(root_, false);
    lv_obj_set_scrollable(root_, false);
    lv_obj_set_hidden(root_, true);
    lv_obj_add_event_cb(root_, &XiaoAnAvatar::OnRootDeleted, LV_EVENT_DELETE, this);

    CreatePart(root_, kLeftArmImage, kLeftArmPosition);
    right_arm_ = CreatePart(root_, kRightArmImage, kRightArmPosition);
    lv_image_set_pivot(right_arm_, kRightArmPivot.x, kRightArmPivot.y);
    CreatePart(root_, kTorsoImage, kTorsoPosition);
    head_ = CreatePart(root_, kHeadImage, kHeadPosition);
    lv_image_set_pivot(head_, kHeadPivot.x, kHeadPivot.y);
    eyes_ = CreatePart(root_, kEyeImages[0], {kEyesCenter.x - kEyesOpen.width / 2,
                                              kEyesCenter.y - kEyesOpen.height / 2});
    mouth_ = CreatePart(root_, kMouthImages[0], {kMouthCenter.x - kSmile.width / 2,
                                                 kMouthCenter.y - kSmile.height / 2});

    timer_ = lv_timer_create(&XiaoAnAvatar::OnTimer, 1, this);
    RestartPose();
}

void XiaoAnAvatar::SetState(DeviceState state) {
    if (state_ == state) return;
    state_ = state;
    emotion_ = Emotion::Neutral;
    RestartPose();
}

void XiaoAnAvatar::SetEmotion(Emotion emotion) {
    if (!screen_visible_) return;
    if (state_ == DeviceState::Starting) {
        emotion_ = emotion;
        Render();  // Complete the startup greeting on its original timeline.
        return;
    }
    if (state_ != DeviceState::Idle && state_ != DeviceState::Offline) return;
    if (emotion_ == Emotion::Neutral && emotion == Emotion::Neutral) return;
    emotion_ = emotion;
    RestartPose();
}

void XiaoAnAvatar::SetScreenVisible(bool visible) {
    if (screen_visible_ == visible) return;
    screen_visible_ = visible;
    emotion_ = Emotion::Neutral;
    RestartPose();
}

void XiaoAnAvatar::OnTimer(lv_timer_t* timer) {
    static_cast<XiaoAnAvatar*>(lv_timer_get_user_data(timer))->Render();
}

void XiaoAnAvatar::OnRootDeleted(lv_event_t* event) {
    auto* self = static_cast<XiaoAnAvatar*>(lv_event_get_user_data(event));
    if (self->timer_) {
        lv_timer_delete(self->timer_);
        self->timer_ = nullptr;
    }
    self->root_ = nullptr;
    self->pose_ = {};
}

void XiaoAnAvatar::RestartPose() {
    transition_from_ = pose_;
    pose_started_at_ = lv_tick_get();
    Render();
}

void XiaoAnAvatar::Render() {
    if (!root_) return;
    const auto next = GetXiaoAnPose(state_, emotion_, screen_visible_,
                                   lv_tick_elaps(pose_started_at_), transition_from_);
    const bool becoming_visible = next.visible && !pose_.visible;
    if (next.visible != pose_.visible) lv_obj_set_hidden(root_, !next.visible);
    if (next.visible) {
        if (becoming_visible || next.arm_angle != pose_.arm_angle) {
            lv_image_set_rotation(right_arm_, next.arm_angle);
        }
        const bool head_moved = becoming_visible || next.head_angle != pose_.head_angle;
        if (head_moved) {
            lv_image_set_rotation(head_, next.head_angle);
            constexpr float kRadiansPerTenth = 0.001745329252f;
            const float radians = next.head_angle * kRadiansPerTenth;
            const float sine = std::sin(radians);
            const float cosine = std::cos(radians);
            PlaceFacePart(eyes_, kEyesOpen, kEyesCenter, sine, cosine, next.head_angle);
            PlaceFacePart(mouth_, kSmile, kMouthCenter, sine, cosine, next.head_angle);
        }
        if (becoming_visible || next.eyes_closed != pose_.eyes_closed) {
            lv_image_set_src(eyes_, &kEyeImages[next.eyes_closed]);
        }
        if (becoming_visible || next.mouth != pose_.mouth) {
            lv_image_set_src(mouth_, &kMouthImages[static_cast<uint8_t>(next.mouth)]);
        }
        if (becoming_visible || next.mouth_scale_y != pose_.mouth_scale_y) {
            lv_image_set_scale_y(mouth_, next.mouth_scale_y);
        }
    }
    pose_ = next;

    if (!timer_) return;
    if (next.next_update_ms == 0) {
        lv_timer_pause(timer_);
    } else {
        lv_timer_set_period(timer_, next.next_update_ms);
        lv_timer_reset(timer_);
        lv_timer_resume(timer_);
    }
}

}  // namespace agentsphere
