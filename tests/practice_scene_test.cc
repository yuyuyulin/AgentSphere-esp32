#include <cassert>

#include "protocol/practice_scene.h"
#include "ui/round_amoled/quick_panel_gesture.h"

using namespace agentsphere;

constexpr auto kEnter = "183db3e0-ff3a-4f67-bc4d-7d6e102c5110";
constexpr auto kExit = "183db3e0-ff3a-4f67-bc4d-7d6e102c5111";
constexpr auto kOpening = "183db3e0-ff3a-4f67-bc4d-7d6e102c5112";
constexpr auto kPractice = "183db3e0-ff3a-4f67-bc4d-7d6e102c5113";

int main() {
    PracticeScene scene;
    assert(scene.MatchesReply(kDailyScene, ""));
    assert(!scene.MatchesReply(kDailyScene, kPractice));
    assert(!scene.MatchesReply(kEnglishScene, kPractice));

    scene.Enter(kEnter, kOpening);
    assert(scene.phase() == PracticePhase::Entering);
    assert(!scene.MatchesReply(kEnglishScene, kPractice));
    assert(scene.Acknowledge(kExit, true, kEnglishScene, kPractice, kOpening) == SceneAck::Ignored);
    assert(scene.Acknowledge(kEnter, true, kEnglishScene, kPractice, kOpening) == SceneAck::Entered);
    assert(scene.Acknowledge(kEnter, true, kEnglishScene, kPractice, kOpening) == SceneAck::Ignored);
    assert(scene.MatchesReply(kEnglishScene, kPractice));
    assert(!scene.MatchesReply(kEnglishScene, kEnter));
    assert(!scene.MatchesReply(kDailyScene, ""));
    assert(!scene.can_repeat());
    scene.Played();
    assert(scene.can_repeat());

    scene.Exit(kExit);
    assert(scene.phase() == PracticePhase::Exiting);
    assert(!scene.can_repeat());
    assert(!scene.MatchesReply(kEnglishScene, kPractice));
    assert(!scene.MatchesReply(kDailyScene, ""));
    assert(scene.Acknowledge(kExit, true, kDailyScene, "", "") == SceneAck::Exited);
    assert(scene.MatchesReply(kDailyScene, ""));
    assert(!scene.MatchesReply(kEnglishScene, kPractice));

    scene.Enter(kEnter, kOpening);
    assert(scene.Acknowledge(kEnter, false, kDailyScene, "", "") == SceneAck::Rejected);
    assert(scene.phase() == PracticePhase::Daily);
    scene.Enter(kEnter, kOpening);
    assert(scene.Acknowledge(kEnter, true, kEnglishScene, "broken", kOpening) == SceneAck::Reconnect);
    assert(!scene.MatchesReply(kDailyScene, ""));
    scene.Reset();
    assert(scene.MatchesReply(kDailyScene, ""));
    assert(!scene.can_repeat());

    assert(OpensEnglishMenu(200, 225, 160, 227));
    assert(!OpensEnglishMenu(200, 225, 190, 225));
    assert(!OpensEnglishMenu(200, 225, 155, 280));
    assert(!OpensEnglishMenu(200, 225, 245, 225));
}
