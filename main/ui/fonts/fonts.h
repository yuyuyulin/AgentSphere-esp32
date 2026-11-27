#pragma once

#include "sdkconfig.h"

#if CONFIG_AS_ENABLE_LVGL
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t font_zh_18;
extern const lv_font_t font_practice_zh_16;

#ifdef __cplusplus
}
#endif
#endif
