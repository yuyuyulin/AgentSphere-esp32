#pragma once

#include "app/state_machine.h"

namespace agentsphere {

enum class XiaoAnFrame : uint8_t {
    Hidden,
    Resting,
    Blink,
    Attentive,
    Thoughtful,
    Talking,
    Wave,
};

struct XiaoAnPose {
    XiaoAnFrame frame;
    // Zero means the current pose only changes in response to a UI event.
    uint32_t next_update_ms = 0;
    int8_t offset_y = 0;
};

inline XiaoAnPose XiaoAnGreeting(uint32_t elapsed_ms, uint32_t end_ms) {
    constexpr uint32_t kStepMs = 70;
    constexpr int8_t kOffsets[] = {0, 2, 4, 2};
    const uint32_t step = elapsed_ms / kStepMs;
    if (step < sizeof(kOffsets)) {
        return {XiaoAnFrame::Wave, kStepMs - elapsed_ms % kStepMs, kOffsets[step]};
    }
    return {XiaoAnFrame::Wave, end_ms > elapsed_ms ? end_ms - elapsed_ms : 0};
}

inline XiaoAnPose GetXiaoAnPose(DeviceState state, Emotion emotion, bool screen_on,
                                uint32_t elapsed_ms) {
    using Frame = XiaoAnFrame;
    constexpr uint32_t kGreetingMs = 900;

    if (!screen_on) {
        return {Frame::Hidden};
    }

    switch (state) {
        case DeviceState::Starting:
            if (elapsed_ms < kGreetingMs) {
                return XiaoAnGreeting(elapsed_ms, kGreetingMs);
            }
            return {emotion == Emotion::Thinking ? Frame::Thoughtful : Frame::Resting};
        case DeviceState::Listening:
            return {Frame::Attentive};
        case DeviceState::Thinking:
            return {Frame::Thoughtful};
        case DeviceState::Speaking: {
            constexpr uint32_t kMouthFrameMs = 280;
            const auto frame = (elapsed_ms / kMouthFrameMs) % 2 == 0 ?
                Frame::Talking : Frame::Resting;
            return {frame, kMouthFrameMs - elapsed_ms % kMouthFrameMs};
        }
        case DeviceState::WifiConfiguring:
        case DeviceState::Activating:
        case DeviceState::Upgrading:
        case DeviceState::Sleeping:
            return {Frame::Hidden};
        case DeviceState::Idle:
        case DeviceState::Offline:
            break;
    }

    switch (emotion) {
        case Emotion::Happy:
            if (elapsed_ms < kGreetingMs) {
                return XiaoAnGreeting(elapsed_ms, kGreetingMs);
            }
            break;
        case Emotion::Dizzy:
            if (elapsed_ms < 160) {
                return {Frame::Blink, 160 - elapsed_ms};
            }
            if (elapsed_ms < 650) {
                return {Frame::Thoughtful, 650 - elapsed_ms};
            }
            break;
        case Emotion::Thinking:
        case Emotion::Confused:
            if (elapsed_ms < 1000) {
                return {Frame::Thoughtful, 1000 - elapsed_ms};
            }
            break;
        case Emotion::Surprised:
            return {Frame::Attentive};
        case Emotion::Sleepy:
            return {Frame::Blink};
        case Emotion::Charging:
        case Emotion::Neutral:
            break;
    }

    constexpr uint32_t kRestMs = 4200;
    constexpr uint32_t kBlinkMs = 130;
    const auto phase = elapsed_ms % (kRestMs + kBlinkMs);
    if (phase < kRestMs) {
        return {Frame::Resting, kRestMs - phase};
    }
    return {Frame::Blink, kRestMs + kBlinkMs - phase};
}

}  // namespace agentsphere
