#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace agentsphere {

enum class DeviceState : uint8_t {
    Starting = 0,
    WifiConfiguring,
    Activating,
    Upgrading,
    Idle,
    Listening,
    Thinking,
    Speaking,
    Offline,
    Sleeping,
};

enum class Emotion : uint8_t {
    Neutral = 0,
    Happy,
    Thinking,
    Surprised,
    Sleepy,
    Confused,
    Dizzy,
    Charging,
};

const char* DeviceStateToString(DeviceState state);
const char* EmotionToString(Emotion emotion);

class StateMachine {
public:
    using Observer = std::function<void(DeviceState old_state, DeviceState new_state)>;

    DeviceState state() const { return state_; }
    Emotion emotion() const { return emotion_; }
    const std::string& active_scene() const { return active_scene_; }
    const std::string& active_turn_id() const { return active_turn_id_; }

    void SetObserver(Observer observer) { observer_ = std::move(observer); }
    bool TransitionTo(DeviceState next);
    void SetEmotion(Emotion emotion) { emotion_ = emotion; }
    void SetActiveScene(const std::string& scene_id) { active_scene_ = scene_id; }
    void SetActiveTurnId(const std::string& turn_id) { active_turn_id_ = turn_id; }
    void ClearActiveTurnId() { active_turn_id_.clear(); }

private:
    DeviceState state_ = DeviceState::Starting;
    Emotion emotion_ = Emotion::Neutral;
    std::string active_scene_ = "default";
    std::string active_turn_id_;
    Observer observer_;
};

}  // namespace agentsphere
