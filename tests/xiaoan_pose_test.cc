#include <cassert>

#include "ui/round_amoled/xiaoan_pose.h"

using namespace agentsphere;

int main() {
    auto pose = GetXiaoAnPose(DeviceState::Idle, Emotion::Neutral, true, 0);
    assert(pose.frame == XiaoAnFrame::Resting && pose.next_update_ms == 4200);
    pose = GetXiaoAnPose(DeviceState::Offline, Emotion::Neutral, true, 4200);
    assert(pose.frame == XiaoAnFrame::Blink && pose.next_update_ms == 130);
    assert(GetXiaoAnPose(DeviceState::Offline, Emotion::Neutral, true, 4330).frame ==
           XiaoAnFrame::Resting);

    assert(GetXiaoAnPose(DeviceState::Starting, Emotion::Neutral, true, 0).frame ==
           XiaoAnFrame::Wave);
    assert(GetXiaoAnPose(DeviceState::Starting, Emotion::Neutral, true, 900).frame ==
           XiaoAnFrame::Resting);
    assert(GetXiaoAnPose(DeviceState::Starting, Emotion::Thinking, true, 900).frame ==
           XiaoAnFrame::Thoughtful);
    pose = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 0);
    assert(pose.frame == XiaoAnFrame::Wave && pose.next_update_ms == 70);
    assert(GetXiaoAnPose(DeviceState::Starting, Emotion::Neutral, true, 140).offset_y == 4);
    assert(GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 350).offset_y == 0);
    assert(GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 900).frame ==
           XiaoAnFrame::Resting);
    pose = GetXiaoAnPose(DeviceState::Offline, Emotion::Dizzy, true, 0);
    assert(pose.frame == XiaoAnFrame::Blink && pose.next_update_ms == 160);

    assert(GetXiaoAnPose(DeviceState::Listening, Emotion::Happy, true, 0).frame ==
           XiaoAnFrame::Attentive);
    assert(GetXiaoAnPose(DeviceState::Thinking, Emotion::Happy, true, 0).frame ==
           XiaoAnFrame::Thoughtful);
    pose = GetXiaoAnPose(DeviceState::Speaking, Emotion::Happy, true, 0);
    assert(pose.frame == XiaoAnFrame::Talking && pose.next_update_ms == 280);
    assert(GetXiaoAnPose(DeviceState::Speaking, Emotion::Happy, true, 280).frame ==
           XiaoAnFrame::Resting);

    assert(GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, false, 0).frame ==
           XiaoAnFrame::Hidden);
    const DeviceState system_views[] = {
        DeviceState::WifiConfiguring, DeviceState::Activating,
        DeviceState::Upgrading, DeviceState::Sleeping,
    };
    for (const auto state : system_views) {
        pose = GetXiaoAnPose(state, Emotion::Happy, true, 0);
        assert(pose.frame == XiaoAnFrame::Hidden && pose.next_update_ms == 0);
    }
}
