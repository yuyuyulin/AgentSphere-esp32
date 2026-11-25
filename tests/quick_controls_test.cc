#include <cassert>

#include "app/quick_controls.h"
#include "audio/microphone_gate.h"
#include "ui/round_amoled/quick_panel_gesture.h"

using agentsphere::MicrophoneGate;
using agentsphere::QuickControls;

int main() {
    using agentsphere::OpensQuickPanel;
    assert(OpensQuickPanel(233, 45, 235, 110));
    assert(OpensQuickPanel(233, 175, 235, 250));
    assert(OpensQuickPanel(233, 232, 248, 272));
    assert(!OpensQuickPanel(233, 232, 248, 209));
    assert(!OpensQuickPanel(180, 50, 260, 110));
    assert(!OpensQuickPanel(233, 45, 235, 55));

    const QuickControls defaults;
    assert(defaults.volume == 95 && defaults.brightness == 80 && !defaults.muted);
    const QuickControls saved{34, 26, true};
    const auto restored = QuickControls::Decode(saved.Encode());
    assert(restored.volume == 34 && restored.brightness == 26 && restored.muted);
    const auto low_brightness = QuickControls::Decode(QuickControls{0, 0, true}.Encode());
    assert(low_brightness.volume == 0 && low_brightness.brightness == 10 && low_brightness.muted);

    MicrophoneGate microphone;
    const auto pending_capture = microphone.epoch();
    assert(microphone.Allows(pending_capture));
    microphone.SetMuted(true);
    assert(!microphone.Allows(pending_capture));
    assert(!microphone.Allows(microphone.epoch()));
    microphone.SetMuted(false);
    assert(!microphone.Allows(pending_capture));
    const auto reopened_capture = microphone.epoch();
    microphone.SetMuted(false);
    assert(microphone.Allows(reopened_capture));
}
