#include <cassert>
#include <string_view>

#include "boards/waveshare_1_75c/axp2101_power_status.h"
#include "ui/round_amoled/xiaoan_status.h"

using namespace agentsphere;
using namespace std::string_view_literals;

int main() {
    const auto charging = DecodeAxp2101PowerStatus(0x28, 0x22, 4, 3582);
    assert(charging.available && charging.usb_plugged && charging.battery_present);
    assert(charging.charging && !charging.charge_complete && charging.battery_mv == 3582);
    assert(charging.battery_percent == 4 && XiaoAnBatteryIconLevel(charging) == 0);
    assert(XiaoAnPanelPowerText(charging) == "充电中"sv);

    auto usb_only = DecodeAxp2101PowerStatus(0x20, 0x25, 4, 0);
    assert(usb_only.usb_plugged && !usb_only.battery_present && !usb_only.charging);
    assert(usb_only.battery_percent == -1 && XiaoAnBatteryIconLevel(usb_only) == -1);
    assert(XiaoAnPanelPowerText(usb_only) == "USB供电"sv);

    const auto waiting = DecodeAxp2101PowerStatus(0x28, 0x05, 4, 3582);
    assert(waiting.battery_percent == charging.battery_percent && !waiting.charging);
    assert(XiaoAnPanelPowerText(waiting) == "USB供电"sv);

    const auto done = DecodeAxp2101PowerStatus(0x28, 0x04, 100, 4200);
    assert(done.charge_complete && !done.charging && XiaoAnBatteryIconLevel(done) == 4);
    assert(XiaoAnPanelPowerText(done) == "充电完成"sv);

    const auto unplugged = DecodeAxp2101PowerStatus(0x08, 0x45, 36, 3750);
    assert(!unplugged.usb_plugged && XiaoAnBatteryIconLevel(unplugged) == 2);
    assert(XiaoAnPanelPowerText(unplugged) == nullptr);

    const auto read_failed = DecodeAxp2101PowerStatus(0x28, 0x22, -1, 0);
    assert(read_failed.charging && XiaoAnBatteryIconLevel(read_failed) == -1);
    assert(XiaoAnBatteryIconLevel(DecodeAxp2101PowerStatus(0x08, 0x45, 255, 0)) == -1);
    assert(XiaoAnBatteryIconLevel(PowerStatus{}) == -1);
}
