#pragma once

#include "app/events.h"
#include "app/state_machine.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace agentsphere {

class Application {
public:
    esp_err_t Init();
    [[noreturn]] void Run();

private:
    void HandleEvent(const AppEvent& ev);
    void HandleTick1Hz();
    void LogMemoryWatermarks() const;

    QueueHandle_t event_queue_ = nullptr;
    StateMachine sm_;
    bool screen_on_ = true;
    uint32_t tick_count_ = 0;
    int emotion_reset_ticks_ = 0;
    TouchPoint last_touch_{};
    PowerStatus last_power_{};
    char last_event_text_[48] = "Boot OK";
};

}  // namespace agentsphere
