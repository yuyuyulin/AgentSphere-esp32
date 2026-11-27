#pragma once
#include "freertos/FreeRTOS.h"
inline int xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
