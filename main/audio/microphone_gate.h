#pragma once

#include <atomic>
#include <cstdint>

namespace agentsphere {

class MicrophoneGate {
public:
    uint32_t epoch() const { return epoch_.load(); }

    bool Allows(uint32_t capture_epoch) const {
        return (capture_epoch & 1U) == 0 && capture_epoch == epoch();
    }

    // The application owns mute changes. Changing it invalidates queued and
    // active captures, including a mute/unmute before the voice task wakes.
    void SetMuted(bool muted) {
        if (((epoch() & 1U) != 0) != muted) ++epoch_;
    }

private:
    std::atomic<uint32_t> epoch_{0};
};

}  // namespace agentsphere
