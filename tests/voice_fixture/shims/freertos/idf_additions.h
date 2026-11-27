#pragma once
#include "freertos/FreeRTOS.h"

inline int xTaskCreatePinnedToCoreWithCaps(void (*entry)(void*), const char*, unsigned, void* data,
                                         int, void*, int, unsigned) {
    std::thread(entry, data).detach();
    return pdPASS;
}
inline void vTaskDeleteWithCaps(void*) {}
