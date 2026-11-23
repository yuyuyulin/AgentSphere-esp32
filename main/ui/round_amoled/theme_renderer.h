#pragma once

#include "sdkconfig.h"

#if CONFIG_AS_ENABLE_LVGL

#include "app/state_machine.h"
#include "lvgl.h"
#include "ui/round_amoled/xiaoan_avatar.h"

namespace agentsphere {

// Presents the board's status indicator and the local XiaoAn avatar.
class ThemeRenderer {
public:
    void Create(lv_obj_t* parent);
    void SetState(DeviceState state);
    void SetEmotion(Emotion emotion);
    void SetScreenVisible(bool visible);

private:
    lv_obj_t* ring_ = nullptr;
    XiaoAnAvatar avatar_;
};

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
