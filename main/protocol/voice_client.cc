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
#include "freertos/idf_additions.h"

namespace agentsphere {
namespace {
constexpr const char* kTag = "VoiceClient";
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
constexpr int64_t kSecond = 1000000;
constexpr int64_t kConnectTimeoutSec = 18;
constexpr int64_t kHelloTimeoutSec = 10;
constexpr int64_t kSceneTimeoutUs = 10 * kSecond;
constexpr size_t kCaptionBytes = 1024;
constexpr char kBilingualFeature[] = "practice_bilingual_v1";
// The 60 ms Opus encoder exceeded 16 KiB on the 1.75C.
// Keep its stack in PSRAM so Wi-Fi and DMA can use internal RAM.
constexpr uint32_t kVoiceStackBytes = 40 * 1024;

const char* VoiceStateToString(VoiceState state) {
    switch (state) {
        case VoiceState::Disabled: return "Disabled";
        case VoiceState::Connecting: return "Connecting";
        case VoiceState::Ready: return "Ready";
        case VoiceState::Listening: return "Listening";
        case VoiceState::Thinking: return "Thinking";
        case VoiceState::Speaking: return "Speaking";
        case VoiceState::EnteringPractice: return "EnteringPractice";
        case VoiceState::ExitingPractice: return "ExitingPractice";
        case VoiceState::Reauth: return "Reauth";
    }
    return "Unknown";
}

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

bool AdvertisesEnglish(const cJSON* message) {
    const auto* scenes = cJSON_GetObjectItemCaseSensitive(message, "scenes");
    if (!cJSON_IsArray(scenes)) return false;
    const cJSON* scene = nullptr;
    cJSON_ArrayForEach(scene, scenes) {
        if (Field(scene, "id") == kEnglishScene) return true;
    }
    return false;
}

std::string BoundedCaption(std::string_view text) {
    size_t size = std::min(text.size(), kCaptionBytes);
    while (size > 0 && size < text.size() &&
           (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80) {
        --size;
    }
    return std::string(text.substr(0, size));
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
                        const std::string& uid, const std::string& token, bool has_practice_ui) {
    Stop();
    if (!audio || !audio->IsReady() || url.rfind("wss://", 0) != 0 ||
        !SafeHeader(uid) || !SafeHeader(token)) return false;
    has_practice_ui_ = has_practice_ui;
    if (!has_practice_ui_) practice_capable_ = false;
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
    stop_ = local_error_ = reconnect_now_ = false;
    {
        std::lock_guard<std::mutex> lock(output_gate_);
        practice_check_pending_ = false;
        practice_notice_ = PracticeNotice::None;
    }
    command_ = Command::None;
    SetState(VoiceState::Connecting);
    const BaseType_t started = xTaskCreatePinnedToCoreWithCaps(
        TaskEntry, "as_voice", kVoiceStackBytes, this, 6, nullptr, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (started != pdPASS) {
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
    const VoiceState prev = state_.exchange(state);
    if (prev == state) return;
    ESP_LOGI(kTag, "Voice state: %s -> %s", VoiceStateToString(prev), VoiceStateToString(state));
    AppEvent ev{};
    ev.type = AppEventType::VoiceChanged;
    xQueueSend(events_, &ev, 0);
}

bool VoiceClient::Button() {
    std::lock_guard<std::mutex> lock(output_gate_);
    if (command_ == Command::ExitPractice || command_ == Command::EnterPractice ||
        command_ == Command::CheckPractice || command_ == Command::RepeatPractice ||
        practice_check_pending_) return false;
    switch (state_.load()) {
        case VoiceState::Ready: {
            if (practice_open_requested_ && !practice_active_) return false;
            const uint32_t mic_epoch = microphone_.epoch();
            if (!microphone_.Allows(mic_epoch)) return false;
            requested_mic_epoch_ = mic_epoch;
            command_ = Command::Start;
            return true;
        }
        case VoiceState::Listening:
            command_ = Command::Finish;
            return true;
        case VoiceState::Thinking:
        case VoiceState::Speaking:
            command_ = Command::Abort;
            audio_->StopOutput();
            return true;
        case VoiceState::Connecting:
            reconnect_now_ = true;
            ESP_LOGI(kTag, "BOOT pressed while Connecting -> waking reconnect");
            return false;
        default:
            return false;
    }
}

bool VoiceClient::EnterPractice() {
    std::lock_guard<std::mutex> lock(output_gate_);
    if (!has_practice_ui_ || practice_active_ || practice_check_pending_) return false;
    const auto state = state_.load();
    if (state == VoiceState::Disabled || state == VoiceState::Connecting) {
        if (state == VoiceState::Connecting) reconnect_now_ = true;
        practice_notice_ = PracticeNotice::Offline;
        return false;
    }
    if (state != VoiceState::Ready || command_ != Command::None) return false;
    practice_notice_ = PracticeNotice::None;
    if (practice_capable_) {
        practice_open_requested_ = true;
        command_ = Command::EnterPractice;
        return true;
    }
    practice_check_pending_ = true;
    command_ = Command::CheckPractice;
    ESP_LOGI(kTag, "English practice tapped -> re-checking capability with server");
    return true;
}

bool VoiceClient::RepeatPractice() {
    std::lock_guard<std::mutex> lock(output_gate_);
    if (state_ != VoiceState::Ready || !practice_open_requested_ || !practice_active_ || !practice_repeat_ ||
        command_ != Command::None) return false;
    command_ = Command::RepeatPractice;
    return true;
}

void VoiceClient::ExitPractice() {
    std::lock_guard<std::mutex> lock(output_gate_);
    practice_notice_ = PracticeNotice::None;
    practice_check_pending_ = false;
    if (!practice_open_requested_.exchange(false)) return;
    command_ = Command::ExitPractice;
    if (state_ == VoiceState::Speaking) audio_->StopOutput();
}

PracticeView VoiceClient::GetPracticeView() {
    std::lock_guard<std::mutex> lock(output_gate_);
    const auto state = state_.load();
    if (!has_practice_ui_ || state == VoiceState::Reauth) return {};
    if (practice_check_pending_) return {PracticePage::Checking, false, {}};
    if (practice_open_requested_) {
        if (!practice_active_) return {PracticePage::Entering, false, {}};
        return {PracticePage::Active, practice_repeat_ && state == VoiceState::Ready,
                practice_caption_, practice_caption_zh_, practice_caption_revision_};
    }
    if (state == VoiceState::Disabled || state == VoiceState::Connecting) {
        return {PracticePage::Offline, false, {}};
    }
    if (practice_notice_ == PracticeNotice::Unavailable) {
        return {PracticePage::Unavailable, false, {}};
    }
    if (practice_notice_ == PracticeNotice::Offline) {
        return {PracticePage::Offline, false, {}};
    }
    return {PracticePage::Available, false, {}};
}

void VoiceClient::Cancel(bool flipped) {
    std::lock_guard<std::mutex> lock(output_gate_);
    if (command_ == Command::ExitPractice) return;
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
    vTaskDeleteWithCaps(nullptr);
}

void VoiceClient::Run() {
    int delay_sec = 2;
    while (!stop_) {
        negotiated_ = link_error_ = false;
        SetState(VoiceState::Connecting);
        const bool ok = socket_.Open(url_, headers_) && Serve();
        const bool revoked = socket_.unauthorized() || state_ == VoiceState::Reauth;
        EndTurn();
        ResetPractice();
        if (!ok && practice_check_pending_.exchange(false)) {
            std::lock_guard<std::mutex> lock(output_gate_);
            practice_notice_ = PracticeNotice::Offline;
        }
        command_ = Command::None;
        socket_.Close();
        if (revoked) {
            practice_capable_ = false;
            std::lock_guard<std::mutex> lock(output_gate_);
            practice_check_pending_ = false;
            practice_notice_ = PracticeNotice::None;
            SetState(VoiceState::Reauth);
            break;
        }
        if (stop_) break;
        SetState(VoiceState::Connecting);
        if (negotiated_) delay_sec = 2;
        if (!stop_ && !reconnect_now_.load()) {
            ESP_LOGW(kTag, "Voice WSS disconnected, retrying in %ds", delay_sec);
        }
        const int64_t retry = esp_timer_get_time() + delay_sec * kSecond + esp_random() % 500000;
        while (!stop_ && !reconnect_now_.load() && esp_timer_get_time() < retry) vTaskDelay(pdMS_TO_TICKS(20));
        if (reconnect_now_.exchange(false)) {
            delay_sec = 2;
            ESP_LOGI(kTag, "Voice WSS immediate reconnect triggered");
        } else {
            delay_sec = std::min(30, delay_sec * 2);
        }
    }
}

bool VoiceClient::Serve() {
    const int64_t connect_deadline = esp_timer_get_time() + kConnectTimeoutSec * kSecond;
    while (!socket_.connected() && !socket_.failed() && !stop_ && esp_timer_get_time() < connect_deadline)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (!socket_.connected() || stop_ || socket_.failed()) {
        if (!stop_ && !socket_.failed()) {
            ESP_LOGW(kTag, "Voice WSS connect timed out after %llds", static_cast<long long>(kConnectTimeoutSec));
        }
        return false;
    }
    const int64_t hello_deadline = esp_timer_get_time() + kHelloTimeoutSec * kSecond;
    Json hello(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(hello.get(), "type", "hello");
    cJSON_AddNumberToObject(hello.get(), "protocol_version", 1);
    cJSON_AddStringToObject(hello.get(), "fw_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(hello.get(), "active_scene_id", kDailyScene);
    if (has_practice_ui_) {
        auto* features = cJSON_AddArrayToObject(hello.get(), "features");
        cJSON_AddItemToArray(features, cJSON_CreateString(kEnglishScene));
        cJSON_AddItemToArray(features, cJSON_CreateString("captions_v1"));
        cJSON_AddItemToArray(features, cJSON_CreateString(kBilingualFeature));
    }
    auto* fmt = cJSON_AddObjectToObject(hello.get(), "audio");
    cJSON_AddStringToObject(fmt, "format", "opus");
    cJSON_AddNumberToObject(fmt, "sample_rate", 16000);
    cJSON_AddNumberToObject(fmt, "channels", 1);
    cJSON_AddNumberToObject(fmt, "frame_duration_ms", 60);
    if (!SendJson(socket_, hello.get())) return false;
    while (!stop_ && !socket_.failed() && !link_error_ && state_ != VoiceState::Reauth) {
        if (!negotiated_ && esp_timer_get_time() >= hello_deadline) {
            ESP_LOGW(kTag, "Voice WSS hello_ack timed out after %llds", static_cast<long long>(kHelloTimeoutSec));
            break;
        }
        Step();
    }
    return negotiated_;
}

void VoiceClient::Step() {
    const auto cmd = command_.exchange(Command::None);
    if (cmd == Command::CheckPractice) {
        link_error_ = reconnect_now_ = true;
        return;
    }
    if (cmd == Command::EnterPractice) {
        if (state_ == VoiceState::Ready && practice_capable_ && practice_open_requested_) BeginPractice();
        else practice_open_requested_ = false;
    }
    if (cmd == Command::RepeatPractice && state_ == VoiceState::Ready && practice_.can_repeat()) BeginRepeat();
    if (cmd == Command::ExitPractice) EndPractice();
    if (practice_.pending() && esp_timer_get_time() >= deadline_us_) {
        local_error_ = link_error_ = reconnect_now_ = true;
        return;
    }
    if (cmd == Command::Start && state_ == VoiceState::Ready) BeginTurn(requested_mic_epoch_);
    if (state_ == VoiceState::Listening && !microphone_.Allows(turn_mic_epoch_)) Abort("button");
    if (cmd == Command::Finish && state_ == VoiceState::Listening) FinishRecording("manual");
    if ((cmd == Command::Abort || cmd == Command::Flip) && !turn_.empty()) Abort(cmd == Command::Flip ? "flip" : "button");

    const bool busy = (state_ == VoiceState::Listening && recording_acknowledged_) ||
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
        if (!microphone_.Allows(turn_mic_epoch_)) {
            Abort("button");
            return;
        }
        if (frame_count_ >= 500 || esp_timer_get_time() >= deadline_us_) {
            FinishRecording("max_duration");
            return;
        }
        if (!recording_acknowledged_ || command_ == Command::ExitPractice) return;
        if (audio_->ReadPcm(opus_.pcm, OpusFrames::kSamples) != ESP_OK) {
            local_error_ = true;
            Abort("timeout");
            return;
        }
        const unsigned n = opus_.Encode();
        if (!n) {
            local_error_ = true;
            Abort("timeout");
        } else if (!stop_ && command_ != Command::ExitPractice && microphone_.Allows(turn_mic_epoch_) &&
                   esp_timer_get_time() < deadline_us_) {
            socket_.Send(opus_.packet, n, true);
            ++frame_count_;
        }
    } else if (!practice_.pending() && !turn_.empty() && esp_timer_get_time() >= deadline_us_) {
        FailReply();
    } else if (state_ == VoiceState::Speaking) PlayNext();
}

void VoiceClient::BeginTurn(uint32_t mic_epoch) {
    if (!microphone_.Allows(mic_epoch)) return;
    if (!opus_.OpenEncoder() || audio_->BeginCapture() != ESP_OK) {
        local_error_ = true;
        opus_.Close();
        return;
    }
    if (!microphone_.Allows(mic_epoch)) {
        opus_.Close();
        return;
    }
    turn_mic_epoch_ = mic_epoch;
    recording_acknowledged_ = practice_.phase() == PracticePhase::Daily;
    turn_ = NewTurn();
    frame_count_ = 0;
    deadline_us_ = esp_timer_get_time() + 30 * kSecond;
    SendControl("listen.start", turn_, "trigger", "button", {}, practice_.id());
    SetState(VoiceState::Listening);
}

void VoiceClient::FinishRecording(const char* reason) {
    if (!recording_acknowledged_) {
        FailReply();
        return;
    }
    opus_.Close();
    SendControl("listen.stop", turn_, "reason", reason, {}, practice_.id());
    deadline_us_ = esp_timer_get_time() + 120 * kSecond;
    SetState(VoiceState::Thinking);
}

void VoiceClient::EndTurn() {
    turn_.clear();
    delivery_.clear();
    opening_turn_ = replay_turn_ = false;
    recording_acknowledged_ = true;
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
    if (!old_turn.empty()) SendControl("abort", old_turn, "reason", reason, {}, practice_.id());
}

void VoiceClient::SendControl(const char* type, const std::string& turn, const char* key,
                              const char* value, const std::string& delivery, std::string_view practice_id) {
    Json obj(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(obj.get(), "type", type);
    cJSON_AddStringToObject(obj.get(), "turn_id", turn.c_str());
    cJSON_AddStringToObject(obj.get(), key, value);
    if (!strcmp(type, "listen.start")) cJSON_AddStringToObject(obj.get(), "scene_id", practice_.scene_id());
    if (!practice_id.empty()) {
        cJSON_AddStringToObject(obj.get(), "practice_id", std::string(practice_id).c_str());
    }
    if (!delivery.empty()) cJSON_AddStringToObject(obj.get(), "delivery_id", delivery.c_str());
    if (!SendJson(socket_, obj.get())) link_error_ = true;
}

void VoiceClient::BeginPractice() {
    turn_ = NewTurn();
    practice_.Enter(NewTurn(), turn_);
    deadline_us_ = esp_timer_get_time() + kSceneTimeoutUs;
    SetState(VoiceState::EnteringPractice);
    SendScene("scene.enter", turn_);
}

void VoiceClient::BeginRepeat() {
    replay_turn_ = true;
    turn_ = NewTurn();
    deadline_us_ = esp_timer_get_time() + 75 * kSecond;
    SetState(VoiceState::Thinking);
    SendScene("scene.repeat", turn_);
}

void VoiceClient::EndPractice() {
    if (practice_.phase() == PracticePhase::Daily || practice_.phase() == PracticePhase::Exiting) return;
    if (practice_.phase() == PracticePhase::Entering) {
        link_error_ = reconnect_now_ = true;
        return;
    }
    practice_open_requested_ = false;
    EndTurn();
    practice_.Exit(NewTurn());
    deadline_us_ = esp_timer_get_time() + kSceneTimeoutUs;
    SetState(VoiceState::ExitingPractice);
    SendScene("scene.exit");
}

void VoiceClient::ResetPractice() {
    std::lock_guard<std::mutex> lock(output_gate_);
    practice_.Reset();
    practice_active_ = practice_repeat_ = practice_open_requested_ = false;
    practice_caption_.clear();
    practice_caption_zh_.clear();
}

void VoiceClient::SendScene(const char* type, const std::string& turn) {
    Json obj(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(obj.get(), "type", type);
    cJSON_AddStringToObject(obj.get(), "scene_id", kEnglishScene);
    if (practice_.pending()) cJSON_AddStringToObject(obj.get(), "request_id", practice_.request().c_str());
    if (!practice_.id().empty()) cJSON_AddStringToObject(obj.get(), "practice_id", practice_.id().c_str());
    if (!turn.empty()) cJSON_AddStringToObject(obj.get(), "turn_id", turn.c_str());
    if (!SendJson(socket_, obj.get())) link_error_ = true;
}

void VoiceClient::HandleSceneAck(const cJSON* obj) {
    const auto request = Field(obj, "request_id");
    if (!practice_.pending() || request != practice_.request()) return;
    const auto* ok = cJSON_GetObjectItemCaseSensitive(obj, "ok");
    if (!cJSON_IsBool(ok)) {
        local_error_ = link_error_ = reconnect_now_ = true;
        return;
    }
    switch (practice_.Acknowledge(request, cJSON_IsTrue(ok), Field(obj, "active_scene_id"),
                                  Field(obj, "practice_id"), Field(obj, "turn_id"))) {
        case SceneAck::Entered:
            opening_turn_ = true;
            practice_active_ = true;
            deadline_us_ = esp_timer_get_time() + 75 * kSecond;
            SetState(VoiceState::Thinking);
            break;
        case SceneAck::Rejected:
            if (Field(obj, "code") == "CAPABILITY_DISABLED") {
                practice_capable_ = false;
                std::lock_guard<std::mutex> lock(output_gate_);
                practice_notice_ = PracticeNotice::Unavailable;
            } else {
                local_error_ = true;
            }
            EndTurn();
            ResetPractice();
            SetState(VoiceState::Ready);
            break;
        case SceneAck::Exited:
            EndTurn();
            ResetPractice();
            SetState(VoiceState::Ready);
            break;
        case SceneAck::Reconnect:
            local_error_ = link_error_ = reconnect_now_ = true;
            break;
        case SceneAck::Ignored:
            break;
    }
}

void VoiceClient::FailReply(bool send_abort) {
    local_error_ = true;
    if (opening_turn_) {
        EndPractice();
    } else if (send_abort) {
        Abort("timeout");
    } else {
        EndTurn();
        SetState(VoiceState::Ready);
    }
}

void VoiceClient::HandleControl(const cJSON* obj) {
    const auto type = Field(obj, "type");
    const auto turn = Field(obj, "turn_id");
    const auto delivery = Field(obj, "delivery_id");
    const auto practice_id = Field(obj, "practice_id");
    const auto code = Field(obj, "code");
    if (type == "device.unbound" || (type == "error" && (code == "AUTH_REQUIRED" || code == "REAUTH_REQUIRED"))) {
        EndTurn();
        ResetPractice();
        practice_capable_ = false;
        SetState(VoiceState::Reauth);
        return;
    }
    if (!negotiated_) {
        const auto* audio = cJSON_GetObjectItemCaseSensitive(obj, "audio");
        if (type != "hello_ack" || Field(obj, "active_scene_id") != kDailyScene ||
            !IsAudioFormat(cJSON_GetObjectItemCaseSensitive(audio, "uplink")) ||
            !IsAudioFormat(cJSON_GetObjectItemCaseSensitive(audio, "downlink"))) {
            ESP_LOGW(kTag, "Unexpected hello response (type=%.*s)", static_cast<int>(type.size()), type.data());
            link_error_ = true;
            return;
        }
        negotiated_ = true;
        practice_capable_ = has_practice_ui_ && AdvertisesEnglish(obj);
        const auto* features = cJSON_GetObjectItemCaseSensitive(obj, "features");
        bilingual_captions_ = practice_capable_ &&
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(features, kBilingualFeature));
        reconnect_now_ = false;
        ESP_LOGI(kTag, "Voice WSS ready (hello_ack), English practice=%s",
                 practice_capable_ ? "available" : "unavailable");
        if (practice_check_pending_.exchange(false)) {
            if (practice_capable_) {
                {
                    std::lock_guard<std::mutex> lock(output_gate_);
                    practice_notice_ = PracticeNotice::None;
                    practice_open_requested_ = true;
                }
                BeginPractice();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(output_gate_);
                practice_notice_ = PracticeNotice::Unavailable;
            }
        } else if (practice_capable_) {
            std::lock_guard<std::mutex> lock(output_gate_);
            if (practice_notice_ == PracticeNotice::Unavailable || practice_notice_ == PracticeNotice::Offline) {
                practice_notice_ = PracticeNotice::None;
            }
        }
        SetState(VoiceState::Ready);
        return;
    }
    if (type == "scene.ack") {
        HandleSceneAck(obj);
        return;
    }
    if (type == "tts.start") {
        accepting_audio_ = false;
        if (delivery.empty() || delivery.size() > 128 || turn.empty() || turn.size() > 128 ||
            (!practice_id.empty() && !PracticeScene::ValidId(practice_id))) {
            link_error_ = true;
            return;
        }
        if (state_ == VoiceState::Speaking) {
            // Binary frames carry no ID. A second header mid-stream cannot be demultiplexed.
            link_error_ = true;
            return;
        }
        if (turn != turn_ || state_ != VoiceState::Thinking || !recording_acknowledged_ ||
            !practice_.MatchesReply(Field(obj, "scene_id"), practice_id)) {
            ESP_LOGW(kTag, "Discarding stale tts.start (state=%s)", VoiceStateToString(state_.load()));
            SendControl("delivery.ack", std::string(turn), "status", "aborted", std::string(delivery), practice_id);
            return;
        }
        std::lock_guard<std::mutex> lock(output_gate_);
        if (stop_ || command_ == Command::Abort || command_ == Command::Flip ||
            command_ == Command::ExitPractice) return;
        if (!opus_.OpenDecoder() || audio_->BeginOutput() != ESP_OK) {
            local_error_ = link_error_ = true;
            return;
        }
        if (practice_.phase() == PracticePhase::Active) {
            practice_caption_ = BoundedCaption(Field(obj, "text"));
            practice_caption_zh_ = bilingual_captions_ ? BoundedCaption(Field(obj, "text_zh")) : "";
            if (!replay_turn_) ++practice_caption_revision_;
        }
        delivery_ = delivery;
        frame_count_ = 0;
        accepting_audio_ = true;
        deadline_us_ = esp_timer_get_time() + 75 * kSecond;
        SetState(VoiceState::Speaking);
    } else if (type == "tts.stop" && !turn_.empty() && turn == turn_ && delivery == delivery_ &&
               state_ == VoiceState::Speaking && practice_.MatchesScope(practice_id)) {
        accepting_audio_ = false;
        if (Field(obj, "reason") == "completed" && frame_count_ > 0) received_stop_ = true;
        else FailReply();
    } else if (type == "listen.ack" && !turn_.empty() && turn == turn_ &&
               state_ == VoiceState::Listening && practice_.MatchesScope(practice_id)) {
        const auto* accepted = cJSON_GetObjectItemCaseSensitive(obj, "accepted");
        if (cJSON_IsTrue(accepted)) recording_acknowledged_ = true;
        if (cJSON_IsFalse(accepted)) FailReply(false);
    } else if (type == "error" && !practice_.pending() && (turn.empty() || turn == turn_) &&
               (practice_id.empty() || practice_.MatchesScope(practice_id)) &&
               (practice_.phase() == PracticePhase::Daily || !turn.empty())) {
        ESP_LOGW(kTag, "Voice turn rejected/error (code=%.*s)", static_cast<int>(code.size()), code.data());
        if (!turn_.empty()) {
            FailReply(false);
        } else {
            local_error_ = true;
            AppEvent ev{};
            ev.type = AppEventType::VoiceChanged;
            xQueueSend(events_, &ev, 0);
        }
    }
}

void VoiceClient::HandleBinary(uint8_t* bytes, size_t len) {
    if (!accepting_audio_ || command_ == Command::Abort || command_ == Command::Flip ||
        command_ == Command::ExitPractice) return;
    if (!len || len > OpusFrames::kMaxPacket || frame_count_ >= 1000 ||
        xRingbufferSend(play_queue_, bytes, len, 0) != pdTRUE) {
        FailReply();
        return;
    }
    ++frame_count_;
}

void VoiceClient::PlayNext() {
    if (command_ == Command::ExitPractice) return;
    size_t len = 0;
    auto* packet = static_cast<uint8_t*>(xRingbufferReceive(play_queue_, &len, 0));
    if (packet) {
        const bool decoded = opus_.Decode(packet, len);
        vRingbufferReturnItem(play_queue_, packet);
        if (!decoded || audio_->WritePcm(opus_.pcm, OpusFrames::kSamples) != ESP_OK) {
            if (command_ != Command::Abort && command_ != Command::Flip &&
                command_ != Command::ExitPractice && !stop_) FailReply();
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
        if (command_ == Command::Abort || command_ == Command::Flip ||
            command_ == Command::ExitPractice || stop_ || socket_.failed()) return;
        turn = turn_;
        delivery = delivery_;
        if (drained) practice_.Played();
        practice_repeat_ = practice_.can_repeat();
        EndTurn();
        SetState(VoiceState::Ready);
    }
    if (!drained) local_error_ = true;
    ESP_LOGI(kTag, "Sending delivery.ack (status=%s)", drained ? "played" : "aborted");
    SendControl("delivery.ack", turn, "status", drained ? "played" : "aborted", delivery, practice_.id());
}

}  // namespace agentsphere
