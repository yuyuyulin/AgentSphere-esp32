#pragma once
#include <chrono>
#include <cstdint>
#include <thread>

using TickType_t = uint32_t;
using QueueHandle_t = void*;
using BaseType_t = int;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr int pdPASS = 1, pdTRUE = 1;
inline TickType_t pdMS_TO_TICKS(unsigned ms) { return ms; }
inline void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
