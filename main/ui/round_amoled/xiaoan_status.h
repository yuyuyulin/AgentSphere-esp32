#pragma once

#include "app/events.h"
#include "app/state_machine.h"

namespace agentsphere {

constexpr const char* XiaoAnStatusText(DeviceState state, Emotion emotion) {
    switch (state) {
        case DeviceState::Starting: return "正在启动";
        case DeviceState::WifiConfiguring: return "配网中";
        case DeviceState::Activating:
            return emotion == Emotion::Confused ? "配对失败，请重试" : "正在检查绑定";
        case DeviceState::Upgrading: return "升级中";
        case DeviceState::Idle:
            return emotion == Emotion::Confused ? "对话失败，请重试" : nullptr;
        case DeviceState::Listening: return "聆听中";
        case DeviceState::Thinking: return "思考中";
        case DeviceState::Speaking: return "说话中";
        case DeviceState::Offline: return "网络断开\n长按 BOOT 配网";
        case DeviceState::Sleeping: return nullptr;
    }
    return nullptr;
}

constexpr bool XiaoAnShowLowBattery(const PowerStatus& power) {
    constexpr int kLowBatteryPercent = 15;
    return power.available && !power.usb_plugged && !power.charging &&
           power.battery_percent >= 0 && power.battery_percent <= kLowBatteryPercent;
}

constexpr int XiaoAnBatteryIconLevel(const PowerStatus& power) {
    const int percent = power.battery_percent;
    if (!power.available || !power.battery_present || percent < 0 || percent > 100) return -1;
    if (percent < 10) return 0;
    if (percent < 35) return 1;
    if (percent < 65) return 2;
    if (percent < 90) return 3;
    return 4;
}

constexpr const char* XiaoAnPanelPowerText(const PowerStatus& power) {
    if (!power.available) return nullptr;
    if (power.charging) return "充电中";
    if (power.charge_complete) return "充电完成";
    if (power.usb_plugged) return "USB供电";
    return power.battery_present ? nullptr : "无电池";
}

}  // namespace agentsphere
