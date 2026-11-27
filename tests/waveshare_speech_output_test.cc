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

void CheckSpeechGainAndLowFrequencyCut() {
    auto speech = Sine(1000, 4000);
    auto rumble = Sine(100, 4000);
    const double speech_before = SettledRms(speech);
    const double rumble_before = SettledRms(rumble);
    SpeechOutput output;
    output.Process(speech.data(), speech.size());
    assert(SettledRms(speech) > 1.27 * speech_before);
    assert(SettledRms(speech) < 1.31 * speech_before);
    output.Reset();
    output.Process(rumble.data(), rumble.size());
    assert(SettledRms(rumble) < 0.18 * rumble_before);
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
    CheckSpeechGainAndLowFrequencyCut();
    CheckCeilingAndFrameBoundaries();
}
