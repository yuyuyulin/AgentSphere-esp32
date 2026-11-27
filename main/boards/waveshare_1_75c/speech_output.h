#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace agentsphere {

class WaveshareSpeechOutput {
public:
    static constexpr int kSampleRateHz = 16000;

    void Reset() {
        delay1_ = 0.0f;
        delay2_ = 0.0f;
    }

    void Process(int16_t* samples, size_t count) {
        // Second-order Butterworth high-pass at 280 Hz for the 1.75C speaker.
        constexpr float kB0 = 0.92519130f;
        constexpr float kB1 = -1.85038260f;
        constexpr float kB2 = 0.92519130f;
        constexpr float kA1 = -1.84477841f;
        constexpr float kA2 = 0.85598679f;
        constexpr float kGain = 1.3f;

        for (size_t i = 0; i < count; ++i) {
            const float input = static_cast<float>(samples[i]);
            const float filtered = kB0 * input + delay1_;
            delay1_ = kB1 * input - kA1 * filtered + delay2_;
            delay2_ = kB2 * input - kA2 * filtered;
            samples[i] = SoftLimit(kGain * filtered);
        }
    }

private:
    static int16_t SoftLimit(float sample) {
        // Reserve headroom for the BSP's +3.5 dB DAC gain at 100% volume.
        constexpr float kKnee = 18000.0f;
        constexpr float kCeiling = 21800.0f;
        const float magnitude = std::abs(sample);
        if (magnitude <= kKnee) return static_cast<int16_t>(sample);

        const float over = magnitude - kKnee;
        const float range = kCeiling - kKnee;
        const float limited = kKnee + range * over / (over + range);
        return static_cast<int16_t>(std::copysign(limited, sample));
    }

    float delay1_ = 0.0f;
    float delay2_ = 0.0f;
};

}  // namespace agentsphere
