#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

#include "app/events.h"
#include "app/practice_view.h"
#include "audio/microphone_gate.h"
#include "audio/opus_frames.h"
#include "boards/board.h"
#include "cJSON.h"
#include "freertos/semphr.h"
#include "protocol/practice_scene.h"
#include "protocol/voice_socket.h"

namespace agentsphere {

enum class VoiceState { Disabled, Connecting, Ready, Listening, Thinking, Speaking, EnteringPractice, ExitingPractice, Reauth };

class VoiceClient {
public:
    bool Start(AudioCodec* audio, QueueHandle_t events, const std::string& url,
               const std::string& uid, const std::string& token, bool has_practice_ui);
    void Stop();
    bool Button();
    bool EnterPractice();
    bool RepeatPractice();
    void ExitPractice();
    PracticeView GetPracticeView();
    void SetMicrophoneMuted(bool muted) { microphone_.SetMuted(muted); }
    void Cancel(bool flipped = false);
    VoiceState state() const { return state_; }
    bool TakeError() { return local_error_.exchange(false); }

private:
    enum class Command {
        None,
        Start,
        Finish,
        Abort,
        Flip,
        EnterPractice,
        CheckPractice,
        RepeatPractice,
        ExitPractice,
    };
    enum class PracticeNotice { None, Offline, Unavailable };
    static void TaskEntry(void* arg);
    void Run();
    bool Serve();
    void SetState(VoiceState state);
    void Step();
    void HandleControl(const cJSON* message);
    void HandleBinary(uint8_t* bytes, size_t len);
    void BeginTurn(uint32_t mic_epoch);
    void BeginPractice();
    void BeginRepeat();
    void EndPractice();
    void ResetPractice();
    void HandleSceneAck(const cJSON* message);
    void SendScene(const char* type, const std::string& turn = {});
    void FailReply(bool send_abort = true);
    void FinishRecording(const char* reason);
    void EndTurn();
    void Abort(const char* reason);
    void PlayNext();
    void SendControl(const char* type, const std::string& turn, const char* key,
                     const char* value, const std::string& delivery = {},
                     std::string_view practice_id = {});

    VoiceSocket socket_;
    OpusFrames opus_;
    AudioCodec* audio_ = nullptr;
    QueueHandle_t events_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    RingbufHandle_t play_queue_ = nullptr;
    std::string url_, headers_;
    std::string turn_, delivery_;
    PracticeScene practice_;
    std::string practice_caption_;  // Guarded by output_gate_.
    PracticeNotice practice_notice_ = PracticeNotice::None;  // Guarded by output_gate_.
    std::atomic<bool> practice_capable_{false}, practice_active_{false}, practice_repeat_{false};
    std::atomic<bool> practice_open_requested_{false}, practice_check_pending_{false};
    bool opening_turn_ = false, recording_acknowledged_ = true;
    int64_t deadline_us_ = 0;
    bool accepting_audio_ = false, received_stop_ = false;
    unsigned frame_count_ = 0;
    bool link_error_ = false, negotiated_ = false, has_practice_ui_ = false;
    std::atomic<bool> stop_{false}, local_error_{false}, reconnect_now_{false};
    std::atomic<VoiceState> state_{VoiceState::Disabled};
    // Serialize physical mute against the one operation that enables output.
    // Network operations and blocking PCM writes never hold this lock.
    std::mutex output_gate_;
    MicrophoneGate microphone_;
    std::atomic<uint32_t> requested_mic_epoch_{0};
    uint32_t turn_mic_epoch_ = 0;
    std::atomic<Command> command_{Command::None};
};

}  // namespace agentsphere
