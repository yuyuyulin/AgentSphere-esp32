#include <cassert>
#include <initializer_list>

#include "app/screen_idle.h"

using agentsphere::DeviceState;
using agentsphere::ScreenIdleTimer;

constexpr int64_t kSecond = 1000000;

int main() {
    ScreenIdleTimer timer;
    assert(!timer.ShouldBlank(0, true, true, DeviceState::Idle));
    assert(!timer.ShouldBlank(60 * kSecond - 1, true, true, DeviceState::Idle));
    assert(timer.ShouldBlank(60 * kSecond, true, true, DeviceState::Idle));

    timer.Reset();
    assert(!timer.ShouldBlank(65 * kSecond, true, true, DeviceState::Offline));
    assert(!timer.ShouldBlank(120 * kSecond, true, true, DeviceState::Offline));
    assert(timer.ShouldBlank(125 * kSecond, true, true, DeviceState::Offline));

    for (const auto state : {DeviceState::Starting, DeviceState::Listening, DeviceState::Thinking,
                             DeviceState::Speaking, DeviceState::WifiConfiguring,
                             DeviceState::Activating, DeviceState::Upgrading}) {
        timer.Reset();
        assert(!timer.ShouldBlank(0, true, true, DeviceState::Idle));
        assert(!timer.ShouldBlank(90 * kSecond, true, true, state));
        assert(!timer.ShouldBlank(100 * kSecond, true, true, DeviceState::Idle));
        assert(timer.ShouldBlank(160 * kSecond, true, true, DeviceState::Idle));
    }

    timer.Reset();
    assert(!timer.ShouldBlank(0, true, true, DeviceState::Activating, false));
    assert(timer.ShouldBlank(60 * kSecond, true, true, DeviceState::Activating, false));

    timer.Reset();
    assert(!timer.ShouldBlank(0, true, true, DeviceState::Offline, false));
    timer.StateChanged(DeviceState::Offline, DeviceState::Activating, false);
    timer.StateChanged(DeviceState::Activating, DeviceState::Idle, false);
    assert(timer.ShouldBlank(60 * kSecond, true, true, DeviceState::Idle, false));

    // Even an entire conversation between two timer polls restarts idle time.
    timer.StateChanged(DeviceState::Idle, DeviceState::Listening, false);
    timer.StateChanged(DeviceState::Listening, DeviceState::Idle, false);
    assert(!timer.ShouldBlank(61 * kSecond, true, true, DeviceState::Idle, false));

    assert(!timer.ShouldBlank(200 * kSecond, true, false, DeviceState::Idle));
    assert(!timer.ShouldBlank(260 * kSecond, false, true, DeviceState::Idle));
    assert(!timer.ShouldBlank(300 * kSecond, true, true, DeviceState::Idle));
    assert(timer.ShouldBlank(360 * kSecond, true, true, DeviceState::Idle));
}
