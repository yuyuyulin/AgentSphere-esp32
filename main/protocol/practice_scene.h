#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace agentsphere {

inline constexpr char kDailyScene[] = "default";
inline constexpr char kEnglishScene[] = "english_practice";

enum class PracticePhase { Daily, Entering, Active, Exiting };
enum class SceneAck { Ignored, Entered, Exited, Rejected, Reconnect };

class PracticeScene {
public:
    static bool ValidId(std::string_view id) {
        if (id.size() != 36) return false;
        for (size_t i = 0; i < id.size(); ++i) {
            const char c = id[i];
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-') return false;
            } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }

    void Enter(std::string request, std::string opening) {
        phase_ = PracticePhase::Entering;
        request_ = std::move(request);
        opening_ = std::move(opening);
    }

    void Exit(std::string request) {
        phase_ = PracticePhase::Exiting;
        request_ = std::move(request);
        can_repeat_ = false;
    }

    void Reset() { *this = {}; }

    SceneAck Acknowledge(std::string_view request, bool ok, std::string_view scene,
                         std::string_view practice_id, std::string_view opening) {
        if (!pending() || request != request_) return SceneAck::Ignored;
        if (phase_ == PracticePhase::Entering) {
            if (!ok && scene == kDailyScene) {
                Reset();
                return SceneAck::Rejected;
            }
            if (!ok || scene != kEnglishScene || !ValidId(practice_id) || opening != opening_) {
                return SceneAck::Reconnect;
            }
            id_ = practice_id;
            phase_ = PracticePhase::Active;
            return SceneAck::Entered;
        }
        if (!ok || scene != kDailyScene) return SceneAck::Reconnect;
        Reset();
        return SceneAck::Exited;
    }

    bool MatchesScope(std::string_view id) const {
        return phase_ == PracticePhase::Daily ? id.empty() :
               phase_ == PracticePhase::Active && id == id_;
    }

    bool MatchesReply(std::string_view scene, std::string_view id) const {
        return scene == scene_id() && MatchesScope(id);
    }

    void Played() {
        if (phase_ == PracticePhase::Active) can_repeat_ = true;
    }

    bool pending() const {
        return phase_ == PracticePhase::Entering || phase_ == PracticePhase::Exiting;
    }
    const char* scene_id() const { return phase_ == PracticePhase::Active ? kEnglishScene : kDailyScene; }
    const std::string& id() const { return id_; }
    const std::string& request() const { return request_; }
    PracticePhase phase() const { return phase_; }
    bool can_repeat() const { return can_repeat_; }

private:
    PracticePhase phase_ = PracticePhase::Daily;
    std::string request_, opening_, id_;
    bool can_repeat_ = false;
};

}  // namespace agentsphere
