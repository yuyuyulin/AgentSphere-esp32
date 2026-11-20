#include "app/state_machine.h"

#include "esp_log.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "StateMachine";
}

const char* DeviceStateToString(DeviceState state) {
    switch (state) {
        case DeviceState::Starting:        return "Starting";
        case DeviceState::WifiConfiguring: return "WifiConfiguring";
        case DeviceState::Activating:      return "Activating";
        case DeviceState::Upgrading:       return "Upgrading";
        case DeviceState::Idle:            return "Idle";
        case DeviceState::Listening:       return "Listening";
        case DeviceState::Thinking:        return "Thinking";
        case DeviceState::Speaking:        return "Speaking";
        case DeviceState::Offline:         return "Offline";
        case DeviceState::Sleeping:        return "Sleeping";
    }
    return "Unknown";
}

const char* EmotionToString(Emotion emotion) {
    switch (emotion) {
        case Emotion::Neutral:   return "neutral";
        case Emotion::Happy:     return "happy";
        case Emotion::Thinking:  return "thinking";
        case Emotion::Surprised: return "surprised";
        case Emotion::Sleepy:    return "sleepy";
        case Emotion::Confused:  return "confused";
        case Emotion::Dizzy:     return "dizzy";
        case Emotion::Charging:  return "charging";
    }
    return "neutral";
}

bool StateMachine::TransitionTo(DeviceState next) {
    if (next == state_) {
        return false;
    }
    const DeviceState prev = state_;
    state_ = next;
    ESP_LOGI(kTag, "State: %s -> %s", DeviceStateToString(prev), DeviceStateToString(next));
    if (observer_) {
        observer_(prev, next);
    }
    return true;
}

}  // namespace agentsphere
