#pragma once

#include <cstdint>

#include "app/state_machine.h"

namespace agentsphere {

class ScreenIdleTimer {
public:
    void Reset() { idle_since_us_ = -1; }
    void StateChanged(DeviceState previous, DeviceState next, bool showing_pairing) {
        // Background reconnect can move between screen-idle states without user input.
        if (!CanBlank(previous, showing_pairing) || !CanBlank(next, showing_pairing)) Reset();
    }

    bool ShouldBlank(int64_t now_us, bool usb_plugged, bool screen_on, DeviceState state,
                     bool showing_pairing = true) {
        if (!usb_plugged || !screen_on || !CanBlank(state, showing_pairing)) {
            Reset();
            return false;
        }
        if (idle_since_us_ < 0) idle_since_us_ = now_us;
        return now_us - idle_since_us_ >= 60 * 1000000LL;
    }

private:
    static bool CanBlank(DeviceState state, bool showing_pairing) {
        return state == DeviceState::Idle || state == DeviceState::Offline ||
               (state == DeviceState::Activating && !showing_pairing);
    }

    int64_t idle_since_us_ = -1;
};

}  // namespace agentsphere
