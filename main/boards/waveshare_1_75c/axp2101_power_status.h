#pragma once

#include "app/events.h"

namespace agentsphere {

// AXP2101 PMU status registers 00h and 01h.
constexpr bool Axp2101HasBattery(uint8_t status1) {
    return (status1 & 0x08U) != 0;
}

constexpr PowerStatus DecodeAxp2101PowerStatus(uint8_t status1, uint8_t status2,
                                               int percentage, int voltage_mv) {
    PowerStatus power;
    power.available = true;
    power.usb_plugged = (status1 & 0x20U) != 0;  // VBUS good
    power.battery_present = Axp2101HasBattery(status1);
    if (power.battery_present) {
        power.battery_mv = voltage_mv;
        if (percentage >= 0 && percentage <= 100) power.battery_percent = percentage;
        if (power.usb_plugged) {
            power.charging = ((status2 >> 5) & 0x03U) == 1U;  // battery current direction
            power.charge_complete = !power.charging && (status2 & 0x07U) == 4U;
        }
    }
    return power;
}

}  // namespace agentsphere
