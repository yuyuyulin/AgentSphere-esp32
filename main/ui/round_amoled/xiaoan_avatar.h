#pragma once

#include "lvgl.h"
#include "ui/round_amoled/xiaoan_pose.h"

namespace agentsphere {

class XiaoAnAvatar {
public:
    // All methods are called under the LVGL display lock.
    void Create(lv_obj_t* parent);
    void SetState(DeviceState state);
    void SetEmotion(Emotion emotion);
    void SetScreenVisible(bool visible);

private:
    static void OnTimer(lv_timer_t* timer);
    static void OnRootDeleted(lv_event_t* event);
    void RestartPose();
    void Render();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* head_ = nullptr;
    lv_obj_t* right_arm_ = nullptr;
    lv_obj_t* eyes_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    DeviceState state_ = DeviceState::Starting;
    Emotion emotion_ = Emotion::Neutral;
    XiaoAnPose pose_{};
    XiaoAnPose transition_from_{};
    uint32_t pose_started_at_ = 0;
    bool screen_visible_ = true;
};

}  // namespace agentsphere
