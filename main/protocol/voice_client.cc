#include "protocol/voice_client.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

namespace agentsphere {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
constexpr int64_t kSecond = 1000000;

std::string_view Field(const cJSON* obj, const char* key) {
    const char* s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, key));
    return s ? s : "";
}

bool IsAudioFormat(const cJSON* format) {
    auto number = [format](const char* key) {
        return cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(format, key));
    };
    return Field(format, "format") == "opus" && number("sample_rate") == 16000 &&
           number("channels") == 1 && number("frame_duration_ms") == 60;
}

std::string NewTurn() {
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    char id[37];
    snprintf(id, sizeof(id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return id;
}

bool SafeHeader(const std::string& value) {
    return !value.empty() && value.size() <= 512 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return c > 32 && c < 127; });
}

bool SendJson(VoiceSocket& socket, cJSON* obj) {
    char* text = cJSON_PrintUnformatted(obj);
    if (!text) return false;
    const bool ok = socket.Send(text, strlen(text));
    cJSON_free(text);
    return ok;
}
}  // namespace

bool VoiceClient::Start(AudioCodec* audio, QueueHandle_t events, const std::string& url,
                        const std::string& uid, const std::string& token) {
    Stop();
    if (!audio || !audio->IsReady() || url.rfind("wss://", 0) != 0 ||
        !SafeHeader(uid) || !SafeHeader(token)) return false;
    audio_ = audio;
    events_ = events;
    done_ = xSemaphoreCreateBinary();
    play_queue_ = xRingbufferCreateWithCaps(64 * 1024, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!done_ || !play_queue_) {
        if (done_) vSemaphoreDelete(done_);
        if (play_queue_) vRingbufferDeleteWithCaps(play_queue_);
        done_ = nullptr;
        play_queue_ = nullptr;
        return false;
    }
    url_ = url;
    headers_ = "Authorization: Bearer " + token + "\r\nX-Device-Uid: " + uid + "\r\nX-Protocol-Version: 1\r\n";
    stop_ = local_error_ = false;
    command_ = Command::None;
    SetState(VoiceState::Connecting);
    if (xTaskCreatePinnedToCore(TaskEntry, "as_voice", 16 * 1024, this, 6, nullptr, 1) != pdPASS) {
        xSemaphoreGive(done_);
        Stop();
        return false;
    }
    return true;
}

void VoiceClient::Stop() {
    if (!done_) return;
    {
        std::lock_guard<std::mutex> lock(output_gate_);
        stop_ = true;
        if (state_ == VoiceState::Speaking) audio_->StopOutput();
    }
    xSemaphoreTake(done_, portMAX_DELAY);
    vSemaphoreDelete(done_);
    done_ = nullptr;
    vRingbufferDeleteWithCaps(play_queue_);
    play_queue_ = nullptr;
    headers_.clear();
    state_ = VoiceState::Disabled;
}

void VoiceClient::SetState(VoiceState state) {
    if (state_.exchange(state) == state) return;
    AppEvent ev{};
    ev.type = AppEventType::VoiceChanged;
    xQueueSend(events_, &ev, 0);
}

void VoiceClient::Button() {
    std::lock_guard<std::mutex> lock(output_gate_);
    switch (state_.load()) {
        case VoiceState::Ready: command_ = Command::Start; break;
        case VoiceState::Listening: command_ = Command::Finish; break;
        case VoiceState::Thinking:
        case VoiceState::Speaking:
            command_ = Command::Abort;
            audio_->StopOutput();
            break;
        default: break;
    }
}

void VoiceClient::Cancel(bool flipped) {
    std::lock_guard<std::mutex> lock(output_gate_);
    const auto state = state_.load();
    if (state == VoiceState::Listening || state == VoiceState::Thinking || state == VoiceState::Speaking) {
        command_ = flipped ? Command::Flip : Command::Abort;
        audio_->StopOutput();
    }
}

void VoiceClient::TaskEntry(void* arg) {
    auto* self = static_cast<VoiceClient*>(arg);
    self->Run();
    xSemaphoreGive(self->done_);
    vTaskDelete(nullptr);
}

void VoiceClient::Run() {
    int delay_sec = 2;
    while (!stop_) {
        negotiated_ = link_error_ = false;
        SetState(VoiceState::Connecting);
        if (socket_.Open(url_, headers_)) Serve();
        const bool revoked = socket_.unauthorized() || state_ == VoiceState::Reauth;
        EndTurn();
        socket_.Close();
        if (revoked) {
            SetState(VoiceState::Reauth);
            break;
        }
        SetState(VoiceState::Connecting);
        if (negotiated_) delay_sec = 2;
        const int64_t retry = esp_timer_get_time() + delay_sec * kSecond + esp_random() % 500000;
        while (!stop_ && esp_timer_get_time() < retry) vTaskDelay(pdMS_TO_TICKS(20));
        delay_sec = std::min(30, delay_sec * 2);
    }
}

bool VoiceClient::Serve() {
    const int64_t hello_deadline = esp_timer_get_time() + 10 * kSecond;
    while (!socket_.connected() && !socket_.failed() && !stop_ && esp_timer_get_time() < hello_deadline)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (!socket_.connected() || stop_ || socket_.failed()) return false;
    Json hello(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(hello.get(), "type", "hello");
    cJSON_AddNumberToObject(hello.get(), "protocol_version", 1);
    cJSON_AddStringToObject(hello.get(), "fw_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(hello.get(), "active_scene_id", "default");
    auto* fmt = cJSON_AddObjectToObject(hello.get(), "audio");
    cJSON_AddStringToObject(fmt, "format", "opus");
    cJSON_AddNumberToObject(fmt, "sample_rate", 16000);
    cJSON_AddNumberToObject(fmt, "channels", 1);
    cJSON_AddNumberToObject(fmt, "frame_duration_ms", 60);
    if (!SendJson(socket_, hello.get())) return false;
    while (!stop_ && !socket_.failed() && !link_error_ && state_ != VoiceState::Reauth) {
        if (!negotiated_ && esp_timer_get_time() >= hello_deadline) break;
        Step();
    }
    return negotiated_;
}

void VoiceClient::Step() {
    const auto cmd = command_.exchange(Command::None);
    if (cmd == Command::Start && state_ == VoiceState::Ready) BeginTurn();
    if (cmd == Command::Finish && state_ == VoiceState::Listening) FinishRecording("manual");
    if ((cmd == Command::Abort || cmd == Command::Flip) && !turn_.empty()) Abort(cmd == Command::Flip ? "flip" : "button");

    const bool busy = state_ == VoiceState::Listening ||
                      state_ == VoiceState::Speaking;
    for (int i = 0; i < 32 && !link_error_ && !socket_.failed(); ++i) {
        size_t len = 0;
        auto* msg = socket_.Receive(&len, i == 0 && !busy ? pdMS_TO_TICKS(10) : 0);
        if (!msg) break;
        if (msg->opcode == 1) {
            Json obj(cJSON_ParseWithLength(reinterpret_cast<const char*>(msg->data), len), cJSON_Delete);
            if (!cJSON_IsObject(obj.get())) link_error_ = true;
            else HandleControl(obj.get());
        } else HandleBinary(msg->data, len);
        socket_.Release(msg);
    }
    if (stop_ || link_error_ || socket_.failed() || state_ == VoiceState::Reauth) return;

    if (state_ == VoiceState::Listening) {
        if (frame_count_ >= 500 || esp_timer_get_time() >= deadline_us_) {
            FinishRecording("max_duration");
            return;
        }
        if (audio_->ReadPcm(opus_.pcm, OpusFrames::kSamples) != ESP_OK) {
            local_error_ = true;
            Abort("timeout");
            return;
        }
        const unsigned n = opus_.Encode();
        if (!n) {
            local_error_ = true;
            Abort("timeout");
        } else if (!stop_ && esp_timer_get_time() < deadline_us_) {
            socket_.Send(opus_.packet, n, true);
            ++frame_count_;
        }
    } else if (!turn_.empty() && esp_timer_get_time() >= deadline_us_) {
        local_error_ = true;
        Abort("timeout");
    } else if (state_ == VoiceState::Speaking) PlayNext();
}

void VoiceClient::BeginTurn() {
    if (!opus_.OpenEncoder() || audio_->BeginCapture() != ESP_OK) {
        local_error_ = true;
        opus_.Close();
        return;
    }
    turn_ = NewTurn();
    frame_count_ = 0;
    deadline_us_ = esp_timer_get_time() + 30 * kSecond;
    SendControl("listen.start", turn_, "trigger", "button");
    SetState(VoiceState::Listening);
}

void VoiceClient::FinishRecording(const char* reason) {
    opus_.Close();
    SendControl("listen.stop", turn_, "reason", reason);
    deadline_us_ = esp_timer_get_time() + 120 * kSecond;
    SetState(VoiceState::Thinking);
}

void VoiceClient::EndTurn() {
    turn_.clear();
    delivery_.clear();
    accepting_audio_ = received_stop_ = false;
    if (state_ == VoiceState::Speaking) audio_->StopOutput();
    opus_.Close();
    size_t n;
    while (auto* item = xRingbufferReceive(play_queue_, &n, 0)) vRingbufferReturnItem(play_queue_, item);
    frame_count_ = 0;
}

void VoiceClient::Abort(const char* reason) {
    const std::string old_turn = turn_;
    EndTurn();
    SetState(VoiceState::Ready);
    if (!old_turn.empty()) SendControl("abort", old_turn, "reason", reason);
}

void VoiceClient::SendControl(const char* type, const std::string& turn, const char* key,
                              const char* value, const std::string& delivery) {
    Json obj(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(obj.get(), "type", type);
    cJSON_AddStringToObject(obj.get(), "turn_id", turn.c_str());
    cJSON_AddStringToObject(obj.get(), key, value);
    if (!strcmp(type, "listen.start")) cJSON_AddStringToObject(obj.get(), "scene_id", "default");
    if (!delivery.empty()) cJSON_AddStringToObject(obj.get(), "delivery_id", delivery.c_str());
    if (!SendJson(socket_, obj.get())) link_error_ = true;
}

void VoiceClient::HandleControl(const cJSON* obj) {
    const auto type = Field(obj, "type");
    const auto turn = Field(obj, "turn_id");
    const auto delivery = Field(obj, "delivery_id");
    const auto code = Field(obj, "code");
    if (type == "device.unbound" || (type == "error" && (code == "AUTH_REQUIRED" || code == "REAUTH_REQUIRED"))) {
        EndTurn();
        SetState(VoiceState::Reauth);
        return;
    }
    if (!negotiated_) {
        const auto* audio = cJSON_GetObjectItemCaseSensitive(obj, "audio");
        if (type != "hello_ack" || Field(obj, "active_scene_id") != "default" ||
            !IsAudioFormat(cJSON_GetObjectItemCaseSensitive(audio, "uplink")) ||
            !IsAudioFormat(cJSON_GetObjectItemCaseSensitive(audio, "downlink"))) {
            link_error_ = true;
            return;
        }
        negotiated_ = true;
        SetState(VoiceState::Ready);
        return;
    }
    if (type == "tts.start") {
        accepting_audio_ = false;
        if (delivery.empty() || delivery.size() > 128 || turn.empty() || turn.size() > 128) {
            link_error_ = true;
            return;
        }
        if (turn != turn_ || state_ != VoiceState::Thinking || Field(obj, "scene_id") != "default") {
            SendControl("delivery.ack", std::string(turn), "status", "aborted", std::string(delivery));
            return;
        }
        std::lock_guard<std::mutex> lock(output_gate_);
        if (stop_ || command_ == Command::Abort || command_ == Command::Flip) return;
        if (!opus_.OpenDecoder() || audio_->BeginOutput() != ESP_OK) {
            local_error_ = link_error_ = true;
            return;
        }
        delivery_ = delivery;
        frame_count_ = 0;
        accepting_audio_ = true;
        deadline_us_ = esp_timer_get_time() + 75 * kSecond;
        SetState(VoiceState::Speaking);
    } else if (type == "tts.stop" && !turn_.empty() && turn == turn_ && delivery == delivery_ && state_ == VoiceState::Speaking) {
        accepting_audio_ = false;
        if (Field(obj, "reason") == "completed" && frame_count_ > 0) received_stop_ = true;
        else Abort("timeout");
    } else if ((type == "error" && (turn.empty() || turn == turn_)) ||
               (type == "listen.ack" && turn == turn_ && cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(obj, "accepted")))) {
        local_error_ = true;
        EndTurn();
        SetState(VoiceState::Ready);
    }
}

void VoiceClient::HandleBinary(uint8_t* bytes, size_t len) {
    if (!accepting_audio_ || command_ == Command::Abort || command_ == Command::Flip) return;
    if (!len || len > OpusFrames::kMaxPacket || frame_count_ >= 1000 ||
        xRingbufferSend(play_queue_, bytes, len, 0) != pdTRUE) {
        local_error_ = true;
        Abort("timeout");
        return;
    }
    ++frame_count_;
}

void VoiceClient::PlayNext() {
    size_t len = 0;
    auto* packet = static_cast<uint8_t*>(xRingbufferReceive(play_queue_, &len, 0));
    if (packet) {
        const bool decoded = opus_.Decode(packet, len);
        vRingbufferReturnItem(play_queue_, packet);
        if (!decoded || audio_->WritePcm(opus_.pcm, OpusFrames::kSamples) != ESP_OK) {
            if (command_ != Command::Abort && command_ != Command::Flip && !stop_) {
                local_error_ = true;
                Abort("timeout");
            }
        }
        return;
    }
    if (!received_stop_) {
        vTaskDelay(pdMS_TO_TICKS(2));
        return;
    }

    const bool drained = audio_->DrainOutput() == ESP_OK;
    std::string turn, delivery;
    {
        std::lock_guard<std::mutex> lock(output_gate_);
        if (command_ == Command::Abort || command_ == Command::Flip || stop_ || socket_.failed()) return;
        turn = turn_;
        delivery = delivery_;
        EndTurn();
        SetState(VoiceState::Ready);
    }
    if (!drained) local_error_ = true;
    SendControl("delivery.ack", turn, "status", drained ? "played" : "aborted", delivery);
}

}  // namespace agentsphere
