#pragma once

#include <algorithm>
#include <cstdint>

namespace agentsphere {

struct QuickControls {
    static constexpr uint8_t kMinBrightness = 10;

    static constexpr uint8_t ClampBrightness(int value) {
        return static_cast<uint8_t>(std::clamp(value, static_cast<int>(kMinBrightness), 100));
    }

    uint8_t volume = 95;
    uint8_t brightness = 80;
    bool muted = false;

    uint32_t Encode() const {
        return volume | (static_cast<uint32_t>(brightness) << 8) |
               (static_cast<uint32_t>(muted) << 16);
    }

    static QuickControls Decode(uint32_t value) {
        return {
            static_cast<uint8_t>(std::min<uint32_t>(value & 0xffU, 100)),
            ClampBrightness(static_cast<int>((value >> 8) & 0xffU)),
            (value & (1U << 16)) != 0,
        };
    }
};

}  // namespace agentsphere
