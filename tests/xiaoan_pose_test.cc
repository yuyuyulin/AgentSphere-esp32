#include <cassert>
#include <string_view>

#include "ui/round_amoled/xiaoan_pose.h"
#include "ui/round_amoled/xiaoan_status.h"

using namespace agentsphere;

int main() {
    using namespace std::string_view_literals;
    assert(XiaoAnStatusText(DeviceState::Starting, Emotion::Neutral) == "正在启动"sv);
    assert(XiaoAnStatusText(DeviceState::Starting, Emotion::Thinking) == "正在搜索网络..."sv);
    assert(XiaoAnStatusText(DeviceState::Idle, Emotion::Neutral) == nullptr);
    assert(XiaoAnStatusText(DeviceState::Idle, Emotion::Happy) == nullptr);
    assert(XiaoAnStatusText(DeviceState::Idle, Emotion::Confused) == "对话失败，请重试"sv);
    assert(XiaoAnStatusText(DeviceState::Listening, Emotion::Neutral) == "聆听中"sv);
    assert(XiaoAnStatusText(DeviceState::Thinking, Emotion::Neutral) == "思考中"sv);
    assert(XiaoAnStatusText(DeviceState::Speaking, Emotion::Neutral) == "说话中"sv);
    assert(XiaoAnStatusText(DeviceState::Offline, Emotion::Confused) == "网络断开\n长按 BOOT 配网"sv);
    assert(XiaoAnStatusText(DeviceState::Activating, Emotion::Neutral) == "正在检查绑定"sv);
    assert(XiaoAnStatusText(DeviceState::Activating, Emotion::Confused) == "配对失败，请重试"sv);
    assert(XiaoAnStatusText(DeviceState::Sleeping, Emotion::Neutral) == nullptr);

    PowerStatus power{};
    assert(!XiaoAnShowLowBattery(power));
    power.available = true;
    assert(!XiaoAnShowLowBattery(power));
    power.battery_percent = 16;
    assert(!XiaoAnShowLowBattery(power));
    power.battery_percent = 15;
    assert(XiaoAnShowLowBattery(power));
    power.usb_plugged = true;
    assert(!XiaoAnShowLowBattery(power));
    power.usb_plugged = false;
    power.charging = true;
    assert(!XiaoAnShowLowBattery(power));

    auto pose = GetXiaoAnPose(DeviceState::Idle, Emotion::Neutral, true, 0);
    assert(pose.visible && !pose.eyes_closed && pose.mouth == XiaoAnMouth::Smile);
    assert(pose.head_angle == 0 && pose.arm_angle == 0 && pose.next_update_ms == 4200);
    pose = GetXiaoAnPose(DeviceState::Offline, Emotion::Neutral, true, 4200);
    assert(pose.eyes_closed && pose.head_angle == 0 && pose.arm_angle == 0 && pose.next_update_ms == 130);
    assert(!GetXiaoAnPose(DeviceState::Offline, Emotion::Neutral, true, 4330).eyes_closed);
    assert(GetXiaoAnPose(DeviceState::Offline, Emotion::Dizzy, true, 0).eyes_closed);

    const auto start = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 0);
    const auto lift = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 100);
    const auto raised = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 200);
    const auto wiggle = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 320);
    const auto settled = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, true, 900);
    assert(start.arm_angle == 0 && start.next_update_ms <= 60);
    assert(lift.arm_angle < start.arm_angle && lift.arm_angle > raised.arm_angle);
    assert(wiggle.arm_angle != raised.arm_angle && settled.arm_angle == 0);
    assert(lift.head_angle == 0 && lift.mouth == XiaoAnMouth::Smile);
    assert(GetXiaoAnPose(DeviceState::Starting, Emotion::Neutral, true, 200).arm_angle == raised.arm_angle);

    const auto interrupted = GetXiaoAnPose(DeviceState::Thinking, Emotion::Happy, true, 0, raised);
    const auto returning = GetXiaoAnPose(DeviceState::Thinking, Emotion::Happy, true, 90, raised);
    const auto thinking = GetXiaoAnPose(DeviceState::Thinking, Emotion::Happy, true, 180, raised);
    assert(interrupted.arm_angle == raised.arm_angle && interrupted.head_angle == 0);
    assert(returning.arm_angle > interrupted.arm_angle && returning.arm_angle < 0);
    assert(thinking.arm_angle == 0 && thinking.head_angle < 0 && thinking.mouth == XiaoAnMouth::Thinking);
    assert(thinking.next_update_ms == 0);
    const auto center = GetXiaoAnPose(DeviceState::Idle, Emotion::Neutral, true, 90, thinking);
    assert(center.head_angle > thinking.head_angle && center.head_angle < 0);
    assert(GetXiaoAnPose(DeviceState::Idle, Emotion::Neutral, true, 180, thinking).head_angle == 0);
    assert(GetXiaoAnPose(DeviceState::Listening, Emotion::Happy, true, 180).head_angle > 0);

    const auto speech_start = GetXiaoAnPose(DeviceState::Speaking, Emotion::Happy, true, 0);
    const auto speech_open = GetXiaoAnPose(DeviceState::Speaking, Emotion::Happy, true, 120);
    assert(speech_start.mouth == XiaoAnMouth::Speaking && speech_start.arm_angle == 0);
    assert(speech_open.mouth == speech_start.mouth && speech_open.mouth_scale_y > speech_start.mouth_scale_y);
    assert(speech_open.head_angle == speech_start.head_angle && speech_open.next_update_ms <= 60);

    pose = GetXiaoAnPose(DeviceState::Idle, Emotion::Happy, false, 50, raised);
    assert(!pose.visible && pose.next_update_ms == 0);
    const DeviceState system_views[] = {
        DeviceState::WifiConfiguring, DeviceState::Activating,
        DeviceState::Upgrading, DeviceState::Sleeping,
    };
    for (const auto state : system_views) {
        pose = GetXiaoAnPose(state, Emotion::Happy, true, 50, raised);
        assert(!pose.visible && pose.next_update_ms == 0);
    }
}
