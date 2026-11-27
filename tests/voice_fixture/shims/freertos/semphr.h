#pragma once
#include <condition_variable>
#include <mutex>
#include "freertos/FreeRTOS.h"

struct Semaphore {
    std::mutex guard;
    std::condition_variable changed;
    bool done = false;
};
using SemaphoreHandle_t = Semaphore*;
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new Semaphore; }
inline void vSemaphoreDelete(SemaphoreHandle_t sem) { delete sem; }
inline void xSemaphoreGive(SemaphoreHandle_t sem) {
    std::lock_guard<std::mutex> lock(sem->guard);
    sem->done = true;
    sem->changed.notify_one();
}
inline void xSemaphoreTake(SemaphoreHandle_t sem, TickType_t) {
    std::unique_lock<std::mutex> lock(sem->guard);
    sem->changed.wait(lock, [sem] { return sem->done; });
}
