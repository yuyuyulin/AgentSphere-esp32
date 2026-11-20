#pragma once

#include "sdkconfig.h"

#if CONFIG_AS_ENABLE_LVGL

#include "app/state_machine.h"
#include "lvgl.h"

namespace agentsphere {

// Decoupled visual persona renderer: V1 uses lightweight AMOLED-friendly vector eyes.
// Future versions can swap this renderer to load full avatar asset packs from `assets` partition.
class ThemeRenderer {
public:
    void Create(lv_obj_t* parent);
    void SetState(DeviceState state);
    void SetEmotion(Emotion emotion);

private:
    lv_obj_t* ring_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
};

}  // namespace agentsphere

#endif  // CONFIG_AS_ENABLE_LVGL
