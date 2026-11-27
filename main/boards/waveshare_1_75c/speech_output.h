#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace agentsphere {

class WaveshareSpeechOutput {
public:
    static constexpr int kSampleRateHz = 16000;
    static constexpr float kLimiterCeiling = 21800.0f;

    void Reset() {
        hp_delay1_ = 0.0f;
        hp_delay2_ = 0.0f;
        eq_delay1_ = 0.0f;
        eq_delay2_ = 0.0f;
        env_ = 0.0f;
    }

    void Process(int16_t* samples, size_t count) {
        // 1) Second-order Butterworth high-pass at 280 Hz for the 1.75C micro-speaker.
        constexpr float kHpB0 = 0.92519130f;
        constexpr float kHpB1 = -1.85038260f;
        constexpr float kHpB2 = 0.92519130f;
        constexpr float kHpA1 = -1.84477841f;
        constexpr float kHpA2 = 0.85598679f;

        // 2) Second-order peaking presence EQ at 2200 Hz (Q=0.9, +2.5 dB) to match
        //    the 1.75C speaker efficiency band and improve speech intelligibility.
        constexpr float kEqB0 = 1.08933092f;
        constexpr float kEqB1 = -0.95099767f;
        constexpr float kEqB2 = 0.37498586f;
        constexpr float kEqA1 = -0.95099767f;
        constexpr float kEqA2 = 0.46431677f;

        // 3) Feed-forward speech DRC: raise quiet/normal syllables by +8.3 dB (2.6x)
        //    while compressing loud transients (4:1 above 5200) before soft limiting.
        constexpr float kAttackAlpha = 0.03076677f;   // tau ~= 2.0 ms @ 16 kHz
        constexpr float kReleaseAlpha = 0.00062480f;  // tau ~= 100.0 ms @ 16 kHz
        constexpr float kThreshold = 5200.0f;
        constexpr float kMakeupGain = 2.6f;

        for (size_t i = 0; i < count; ++i) {
            const float input = static_cast<float>(samples[i]);

            const float hp = kHpB0 * input + hp_delay1_;
            hp_delay1_ = kHpB1 * input - kHpA1 * hp + hp_delay2_;
            hp_delay2_ = kHpB2 * input - kHpA2 * hp;

            const float eq = kEqB0 * hp + eq_delay1_;
            eq_delay1_ = kEqB1 * hp - kEqA1 * eq + eq_delay2_;
            eq_delay2_ = kEqB2 * hp - kEqA2 * eq;

            const float mag = std::abs(eq);
            const float alpha = mag > env_ ? kAttackAlpha : kReleaseAlpha;
            env_ += alpha * (mag - env_);

            float gain = kMakeupGain;
            if (env_ > kThreshold) {
                // 4:1 compression => gain reduction = (T / env)^(1 - 1/4) = (T / env)^0.75
                const float ratio = kThreshold / env_;
                const float r_sqrt = std::sqrt(ratio);
                gain *= r_sqrt * std::sqrt(r_sqrt);
            }

            samples[i] = SoftLimit(gain * eq);
        }
    }

private:
    static int16_t SoftLimit(float sample) {
        // Reserve headroom for the BSP's +3.5 dB DAC gain at 100% volume.
        constexpr float kKnee = 18500.0f;
        const float magnitude = std::abs(sample);
        if (magnitude <= kKnee) return static_cast<int16_t>(sample);

        const float over = magnitude - kKnee;
        const float range = kLimiterCeiling - kKnee;
        const float limited = kKnee + range * over / (over + range);
        return static_cast<int16_t>(std::copysign(limited, sample));
    }

    float hp_delay1_ = 0.0f;
    float hp_delay2_ = 0.0f;
    float eq_delay1_ = 0.0f;
    float eq_delay2_ = 0.0f;
    float env_ = 0.0f;
};

}  // namespace agentsphere
