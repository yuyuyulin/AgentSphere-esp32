#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "boards/waveshare_1_75c/speech_output.h"

namespace {

using SpeechOutput = agentsphere::WaveshareSpeechOutput;
using Signal = std::array<int16_t, 1600>;

Signal Sine(double frequency, double amplitude) {
    constexpr double kTwoPi = 6.283185307179586;
    Signal samples{};
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<int16_t>(
            amplitude * std::sin(kTwoPi * frequency * i / SpeechOutput::kSampleRateHz));
    }
    return samples;
}

double SettledRms(const Signal& samples) {
    double squares = 0;
    for (size_t i = samples.size() / 2; i < samples.size(); ++i) {
        squares += static_cast<double>(samples[i]) * samples[i];
    }
    return std::sqrt(squares / (samples.size() / 2));
}

void CheckSpeechGainPresenceAndLowFrequencyCut() {
    auto speech_1k = Sine(1000, 3000);
    auto speech_2k2 = Sine(2200, 3000);
    auto rumble = Sine(100, 4000);
    const double speech_1k_before = SettledRms(speech_1k);
    const double speech_2k2_before = SettledRms(speech_2k2);
    const double rumble_before = SettledRms(rumble);

    SpeechOutput output;
    output.Process(speech_1k.data(), speech_1k.size());
    const double gain_1k = SettledRms(speech_1k) / speech_1k_before;
    assert(gain_1k > 2.6 && gain_1k < 2.95);

    output.Reset();
    output.Process(speech_2k2.data(), speech_2k2.size());
    const double gain_2k2 = SettledRms(speech_2k2) / speech_2k2_before;
    assert(gain_2k2 > gain_1k + 0.45);
    assert(gain_2k2 < 3.6);

    output.Reset();
    output.Process(rumble.data(), rumble.size());
    assert(SettledRms(rumble) < 0.36 * rumble_before);
}

void CheckDynamicRangeCompression() {
    auto quiet = Sine(1000, 3000);
    auto loud = Sine(1000, 14000);
    const double quiet_before = SettledRms(quiet);
    const double loud_before = SettledRms(loud);

    SpeechOutput output;
    output.Process(quiet.data(), quiet.size());
    const double quiet_gain = SettledRms(quiet) / quiet_before;

    output.Reset();
    output.Process(loud.data(), loud.size());
    const double loud_gain = SettledRms(loud) / loud_before;

    // Quiet speech receives full makeup gain while loud passages are compressed
    // smoothly so average loudness rises without hard-clipping transients.
    assert(quiet_gain > 2.5);
    assert(loud_gain > 1.1 && loud_gain < 1.6);
    assert(SettledRms(loud) > SettledRms(quiet));
}

void CheckCeilingAndFrameBoundaries() {
    auto entire = Sine(1000, 32767);
    auto split = entire;
    SpeechOutput reference;
    reference.Process(entire.data(), entire.size());

    SpeechOutput output;
    output.Process(split.data(), 960);
    output.Process(split.data() + 960, split.size() - 960);
    assert(split == entire);
    const auto [low, high] = std::minmax_element(split.begin(), split.end());
    assert(*low > -21800 && *low < -18000);
    assert(*high < 21800 && *high > 18000);

    output.Reset();
    constexpr std::array<int16_t, 160> kSilence{};
    auto silence = kSilence;
    output.Process(silence.data(), silence.size());
    assert(silence == kSilence);
}

}  // namespace

int main() {
    CheckSpeechGainPresenceAndLowFrequencyCut();
    CheckDynamicRangeCompression();
    CheckCeilingAndFrameBoundaries();
}
