#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "app/events.h"
#include "audio/opus_frames.h"
#include "boards/board.h"
#include "cJSON.h"
#include "freertos/semphr.h"
#include "protocol/voice_socket.h"

namespace agentsphere {

enum class VoiceState { Disabled, Connecting, Ready, Listening, Thinking, Speaking, Reauth };

class VoiceClient {
public:
    bool Start(AudioCodec* audio, QueueHandle_t events, const std::string& url,
               const std::string& uid, const std::string& token);
    void Stop();
    void Button();
    void Cancel(bool flipped = false);
    VoiceState state() const { return state_; }
    bool TakeError() { return local_error_.exchange(false); }

private:
    enum class Command { None, Start, Finish, Abort, Flip };
    static void TaskEntry(void* arg);
    void Run();
    bool Serve();
    void SetState(VoiceState state);
    void Step();
    void HandleControl(const cJSON* message);
    void HandleBinary(uint8_t* bytes, size_t len);
    void BeginTurn();
    void FinishRecording(const char* reason);
    void EndTurn();
    void Abort(const char* reason);
    void PlayNext();
    void SendControl(const char* type, const std::string& turn, const char* key,
                     const char* value, const std::string& delivery = {});

    VoiceSocket socket_;
    OpusFrames opus_;
    AudioCodec* audio_ = nullptr;
    QueueHandle_t events_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    RingbufHandle_t play_queue_ = nullptr;
    std::string url_, headers_;
    std::string turn_, delivery_;
    int64_t deadline_us_ = 0;
    bool accepting_audio_ = false, received_stop_ = false;
    unsigned frame_count_ = 0;
    bool link_error_ = false, negotiated_ = false;
    std::atomic<bool> stop_{false}, local_error_{false};
    std::atomic<VoiceState> state_{VoiceState::Disabled};
    // Serialize physical mute against the one operation that enables output.
    // Network operations and blocking PCM writes never hold this lock.
    std::mutex output_gate_;
    std::atomic<Command> command_{Command::None};
};

}  // namespace agentsphere
