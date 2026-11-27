#pragma once

#include <algorithm>
#include <cstddef>

#include "app/state_machine.h"

namespace agentsphere {

enum class XiaoAnMouth : uint8_t { Smile, Speaking, Thinking };

struct XiaoAnPose {
    bool visible = false;
    bool eyes_closed = false;
    XiaoAnMouth mouth = XiaoAnMouth::Smile;
    int16_t head_angle = 0;  // Tenths of a degree, clockwise.
    int16_t arm_angle = 0;
    uint16_t mouth_scale_y = 256;
    uint32_t next_update_ms = 0;
};

namespace xiaoan_motion {

constexpr uint32_t kStepMs = 60;
constexpr uint32_t kTurnMs = 180;
constexpr uint32_t kWaveMs = 900;

inline void SchedulePose(XiaoAnPose& pose, uint32_t delay) {
    if (pose.next_update_ms == 0 || delay < pose.next_update_ms) pose.next_update_ms = delay;
}

inline int16_t Ease(int16_t from, int16_t to, uint32_t elapsed, uint32_t duration) {
    if (elapsed >= duration) return to;
    constexpr int32_t kOne = 1024;
    const auto t = static_cast<int32_t>(elapsed * kOne / duration);
    const auto weight = (t * t / kOne) * (3 * kOne - 2 * t) / kOne;
    return static_cast<int16_t>(from + (to - from) * weight / kOne);
}

struct Keyframe {
    uint32_t time_ms;
    int16_t value;
};

template <std::size_t Count>
int16_t Sample(XiaoAnPose& pose, const Keyframe (&frames)[Count], uint32_t elapsed) {
    for (std::size_t i = 1; i < Count; ++i) {
        if (elapsed >= frames[i].time_ms) continue;
        const auto& previous = frames[i - 1];
        const auto& next = frames[i];
        SchedulePose(pose, std::min(kStepMs, next.time_ms - elapsed));
        return Ease(previous.value, next.value, elapsed - previous.time_ms,
                    next.time_ms - previous.time_ms);
    }
    return frames[Count - 1].value;
}

}  // namespace xiaoan_motion

inline XiaoAnPose GetXiaoAnPose(DeviceState state, Emotion emotion, bool screen_on,
                                uint32_t elapsed_ms, const XiaoAnPose& from = {}) {
    using namespace xiaoan_motion;
    if (!screen_on || state == DeviceState::WifiConfiguring || state == DeviceState::Activating ||
        state == DeviceState::Upgrading || state == DeviceState::Sleeping) return {};

    XiaoAnPose pose;
    pose.visible = true;
    int16_t target_head = 0;
    const bool standby = state == DeviceState::Idle || state == DeviceState::Offline;
    const bool waving = elapsed_ms < kWaveMs &&
                        (state == DeviceState::Starting || (standby && emotion == Emotion::Happy));
    if (waving) {
        const Keyframe wave[] = {{0, from.arm_angle}, {200, -1020}, {320, -790},
                                 {460, -1060}, {600, -850}, {kWaveMs, 0}};
        pose.arm_angle = Sample(pose, wave, elapsed_ms);
    } else if (from.arm_angle != 0 && elapsed_ms < kTurnMs) {
        pose.arm_angle = Ease(from.arm_angle, 0, elapsed_ms, kTurnMs);
        SchedulePose(pose, std::min(kStepMs, kTurnMs - elapsed_ms));
    }

    switch (state) {
        case DeviceState::Listening:
            target_head = 45;
            break;
        case DeviceState::Thinking:
            pose.mouth = XiaoAnMouth::Thinking;
            target_head = -50;
            break;
        case DeviceState::Speaking: {
            pose.mouth = XiaoAnMouth::Speaking;
            constexpr Keyframe speech[] = {{0, 115}, {120, 256}, {210, 125},
                                           {300, 215}, {430, 115}};
            pose.mouth_scale_y = Sample(pose, speech, elapsed_ms % speech[4].time_ms);
            break;
        }
        case DeviceState::Starting:
            if (!waving && emotion == Emotion::Thinking) pose.mouth = XiaoAnMouth::Thinking;
            break;
        case DeviceState::Idle:
        case DeviceState::Offline:
            if (waving) break;
            if (emotion == Emotion::Sleepy) {
                pose.eyes_closed = true;
            } else if (emotion == Emotion::Dizzy && elapsed_ms < 650) {
                pose.eyes_closed = elapsed_ms < 160;
                pose.mouth = XiaoAnMouth::Thinking;
                SchedulePose(pose, (pose.eyes_closed ? 160 : 650) - elapsed_ms);
            } else if ((emotion == Emotion::Thinking || emotion == Emotion::Confused) && elapsed_ms < 1000) {
                pose.mouth = XiaoAnMouth::Thinking;
                SchedulePose(pose, 1000 - elapsed_ms);
            } else {
                constexpr uint32_t kRestMs = 4200;
                constexpr uint32_t kBlinkMs = 130;
                const auto phase = elapsed_ms % (kRestMs + kBlinkMs);
                pose.eyes_closed = phase >= kRestMs;
                SchedulePose(pose, (pose.eyes_closed ? kRestMs + kBlinkMs : kRestMs) - phase);
            }
            break;
        default:
            break;
    }

    pose.head_angle = Ease(from.head_angle, target_head, elapsed_ms, kTurnMs);
    if (from.head_angle != target_head && elapsed_ms < kTurnMs) {
        SchedulePose(pose, std::min(kStepMs, kTurnMs - elapsed_ms));
    }
    return pose;
}

}  // namespace agentsphere
