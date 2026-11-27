#pragma once
template <class... Args> inline void IgnoreLog(Args...) {}
#define ESP_LOGI(...) IgnoreLog(__VA_ARGS__)
#define ESP_LOGW(...) IgnoreLog(__VA_ARGS__)
