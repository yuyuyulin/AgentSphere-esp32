#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>
#include <utility>

#include "protocol/voice_client.h"

using namespace agentsphere;
using namespace std::chrono_literals;

namespace {

constexpr char kPracticeId[] = "6dd88ac4-35e6-4014-b7b8-b9b4bbe2d879";
constexpr char kOldPracticeId[] = "6dd88ac4-35e6-4014-b7b8-b9b4bbe2d878";
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

using Fields = std::initializer_list<std::pair<const char*, std::string>>;

const char* Field(const cJSON* doc, const char* key) {
    const char* value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(doc, key));
    return value ? value : "";
}

struct TestWire {
    std::mutex guard;
    std::condition_variable changed;
    std::deque<std::pair<uint8_t, std::string>> incoming;
    std::deque<std::string> outgoing;
    std::atomic<unsigned> sent_pcm{0};
    std::atomic<int64_t> extra_us{0};

    void Feed(uint8_t opcode, std::string payload = {}) {
        std::lock_guard<std::mutex> lock(guard);
        incoming.emplace_back(opcode, std::move(payload));
        changed.notify_all();
    }

    void Feed(Fields fields, const char* flag = nullptr, bool value = true) {
        Json object(cJSON_CreateObject(), cJSON_Delete);
        for (const auto& [key, text] : fields) cJSON_AddStringToObject(object.get(), key, text.c_str());
        if (flag) cJSON_AddBoolToObject(object.get(), flag, value);
        auto* text = cJSON_PrintUnformatted(object.get());
        Feed(1, text);
        cJSON_free(text);
    }

    Json Take(std::string_view type) {
        std::unique_lock<std::mutex> lock(guard);
        const auto until = std::chrono::steady_clock::now() + 3s;
        while (true) {
            while (!outgoing.empty()) {
                Json object(cJSON_Parse(outgoing.front().c_str()), cJSON_Delete);
                outgoing.pop_front();
                assert(object);
                if (type == Field(object.get(), "type")) return object;
            }
            if (changed.wait_until(lock, until) == std::cv_status::timeout) {
                std::fprintf(stderr, "Missing device frame: %.*s\n", static_cast<int>(type.size()), type.data());
                std::abort();
            }
        }
    }
};

TestWire* wire = nullptr;

template <class Predicate>
void WaitUntil(const Predicate& predicate) {
    for (int i = 0; i < 400 && !predicate(); ++i) std::this_thread::sleep_for(5ms);
    assert(predicate());
}

class AudioSpy final : public AudioCodec {
public:
    esp_err_t Init() override { return ESP_OK; }
    bool IsReady() const override { return true; }
    esp_err_t SetOutputVolume(int) override { return ESP_OK; }
    esp_err_t PlayTone(int, int) override { return ESP_OK; }
    esp_err_t RecordAndLoopback(int) override { return ESP_OK; }
    esp_err_t BeginCapture() override { return ESP_OK; }
    esp_err_t ReadPcm(int16_t*, size_t) override {
        std::this_thread::sleep_for(1ms);
        return ESP_OK;
    }
    esp_err_t BeginOutput() override {
        playing = true;
        ++starts;
        return ESP_OK;
    }
    esp_err_t WritePcm(int16_t*, size_t) override {
        if (!playing) return ESP_FAIL;
        ++writes;
        return ESP_OK;
    }
    esp_err_t DrainOutput() override { return playing ? ESP_OK : ESP_FAIL; }
    esp_err_t StopOutput() override {
        playing = false;
        ++stops;
        return ESP_OK;
    }
    std::atomic<bool> playing{false};
    std::atomic<unsigned> starts{0}, writes{0}, stops{0};
};

class TestDevice {
public:
    explicit TestDevice(bool has_practice_ui = true) : has_practice_ui_(has_practice_ui) {
        wire = &network;
        assert(client.Start(&audio, nullptr, "wss://voice.example", "fixture-device", "fixture-token", has_practice_ui));
    }
    ~TestDevice() {
        client.Stop();
        wire = nullptr;
    }

    void Hello(bool english) {
        const auto hello = network.Take("hello");
        const auto* features = cJSON_GetObjectItemCaseSensitive(hello.get(), "features");
        if (has_practice_ui_) {
            assert(cJSON_IsArray(features) && cJSON_GetArraySize(features) == 2);
            assert(std::string_view(cJSON_GetStringValue(cJSON_GetArrayItem(features, 0))) == kEnglishScene);
            assert(std::string_view(cJSON_GetStringValue(cJSON_GetArrayItem(features, 1))) == "captions_v1");
        } else {
            assert(!features);
        }
        constexpr auto format = R"({"format":"opus","sample_rate":16000,"channels":1,"frame_duration_ms":60})";
        const std::string scenes = english ? R"([{"id":"default"},{"id":"english_practice"}])" :
                                             R"([{"id":"default"}])";
        network.Feed(1, std::string(R"({"type":"hello_ack","active_scene_id":"default","audio":{"uplink":)") +
                     format + R"(,"downlink":)" + format + R"(},"scenes":)" + scenes + "}");
        WaitUntil([&] { return client.state() == VoiceState::Ready; });
    }

    void StartReply(const std::string& tid, const char* session = kPracticeId) {
        network.Feed({{"type", "tts.start"}, {"scene_id", kEnglishScene}, {"turn_id", tid},
                      {"practice_id", session}, {"delivery_id", "fixture-delivery"},
                      {"text", "What flavor do you like?"}});
    }

    void FinishReply(const std::string& tid) {
        network.Feed(2, "packet");
        network.Feed({{"type", "tts.stop"}, {"turn_id", tid}, {"practice_id", kPracticeId},
                      {"delivery_id", "fixture-delivery"}, {"reason", "completed"}});
        const auto ack = network.Take("delivery.ack");
        assert(std::string_view(Field(ack.get(), "practice_id")) == kPracticeId);
        assert(std::string_view(Field(ack.get(), "status")) == "played");
        WaitUntil([&] { return client.GetPracticeView().can_repeat; });
    }

    void Enter(std::string& request, std::string& opening) {
        assert(client.EnterPractice());
        auto sent = network.Take("scene.enter");
        request = Field(sent.get(), "request_id");
        opening = Field(sent.get(), "turn_id");
        assert(PracticeScene::ValidId(request) && PracticeScene::ValidId(opening));
    }

    TestWire network;
    AudioSpy audio;
    VoiceClient client;

private:
    bool has_practice_ui_;
};

void OldServer() {
    TestDevice test;
    test.Hello(false);
    // Screened devices keep the English practice entry swipeable even before server enablement.
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    // Tapping the entry re-checks with the server and surfaces Unavailable when still disabled.
    assert(test.client.EnterPractice());
    WaitUntil([&] { return test.client.GetPracticeView().page == PracticePage::Checking; });
    test.Hello(false);
    assert(test.client.GetPracticeView().page == PracticePage::Unavailable);
    // Closing the menu clears the notice and daily voice keeps working normally.
    test.client.ExitPractice();
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    assert(test.client.Button());
    const auto start = test.network.Take("listen.start");
    assert(std::string_view(Field(start.get(), "scene_id")) == kDailyScene);
    assert(!*Field(start.get(), "practice_id"));
    WaitUntil([&] { return test.client.state() == VoiceState::Listening && test.network.sent_pcm > 0; });
    assert(test.client.Button());
    const auto stop = test.network.Take("listen.stop");
    assert(!*Field(stop.get(), "practice_id"));
    std::puts("PASS default voice + on-tap unavailable prompt with legacy server");
}

void DynamicEnableOnTap() {
    TestDevice test;
    test.Hello(false);
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    assert(test.client.EnterPractice());
    WaitUntil([&] { return test.client.GetPracticeView().page == PracticePage::Checking; });
    // Server now advertises english_practice on fresh handshake -> auto-enters practice!
    const auto hello = test.network.Take("hello");
    assert(hello);
    constexpr auto format = R"({"format":"opus","sample_rate":16000,"channels":1,"frame_duration_ms":60})";
    test.network.Feed(1, std::string(R"({"type":"hello_ack","active_scene_id":"default","audio":{"uplink":)") +
                      format + R"(,"downlink":)" + format +
                      R"(},"scenes":[{"id":"default"},{"id":"english_practice"}]})");
    const auto enter = test.network.Take("scene.enter");
    const std::string request = Field(enter.get(), "request_id");
    const std::string opening = Field(enter.get(), "turn_id");
    assert(PracticeScene::ValidId(request) && PracticeScene::ValidId(opening));
    test.network.Feed({{"type", "scene.ack"}, {"request_id", request}, {"active_scene_id", kEnglishScene},
                       {"practice_id", kPracticeId}, {"turn_id", opening}}, "ok");
    WaitUntil([&] { return test.client.GetPracticeView().page == PracticePage::Active; });
    std::puts("PASS on-tap re-check auto-enters practice when server enabled");
}

void HeadlessServer() {
    TestDevice test(false);
    test.Hello(true);
    assert(test.client.GetPracticeView().page == PracticePage::Hidden);
    assert(test.client.Button());
    const auto start = test.network.Take("listen.start");
    assert(std::string_view(Field(start.get(), "scene_id")) == kDailyScene);
    std::puts("PASS headless client keeps only daily voice");
}

void PracticeRoundTrip() {
    TestDevice test;
    test.Hello(true);
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    std::string request, opening;
    test.Enter(request, opening);
    test.StartReply(opening);  // The opening turn becomes eligible only after scene.ack.
    test.network.Feed(2, "stale-packet");
    const auto premature = test.network.Take("delivery.ack");
    assert(std::string_view(Field(premature.get(), "status")) == "aborted");
    assert(test.audio.starts == 0);
    test.network.Feed({{"type", "scene.ack"}, {"request_id", request}, {"active_scene_id", kEnglishScene},
                       {"practice_id", kPracticeId}, {"turn_id", opening}}, "ok");
    test.StartReply(opening);
    test.FinishReply(opening);
    assert(test.client.GetPracticeView().caption == "What flavor do you like?");
    assert(test.network.sent_pcm == 0);

    assert(test.client.RepeatPractice());
    const auto repeat = test.network.Take("scene.repeat");
    const std::string replay = Field(repeat.get(), "turn_id");
    assert(replay != opening);
    assert(std::string_view(Field(repeat.get(), "practice_id")) == kPracticeId);
    test.StartReply(replay);
    test.FinishReply(replay);

    test.client.SetMicrophoneMuted(true);
    assert(!test.client.Button());
    test.client.SetMicrophoneMuted(false);
    assert(test.client.Button());
    const auto start = test.network.Take("listen.start");
    const std::string answer = Field(start.get(), "turn_id");
    assert(std::string_view(Field(start.get(), "practice_id")) == kPracticeId);
    std::this_thread::sleep_for(35ms);
    assert(test.network.sent_pcm == 0);
    test.network.Feed({{"type", "listen.ack"}, {"turn_id", answer}}, "accepted");
    std::this_thread::sleep_for(35ms);
    assert(test.network.sent_pcm == 0);
    test.network.Feed({{"type", "listen.ack"}, {"turn_id", answer}, {"practice_id", kPracticeId}}, "accepted");
    WaitUntil([&] { return test.network.sent_pcm > 0; });
    assert(test.client.Button());
    const auto stop = test.network.Take("listen.stop");
    assert(std::string_view(Field(stop.get(), "practice_id")) == kPracticeId);

    test.StartReply(answer, kOldPracticeId);
    const auto wrong = test.network.Take("delivery.ack");
    assert(std::string_view(Field(wrong.get(), "status")) == "aborted");
    assert(std::string_view(Field(wrong.get(), "practice_id")) == kOldPracticeId);
    test.StartReply(answer);
    test.FinishReply(answer);

    assert(test.client.Button());
    const auto next = test.network.Take("listen.start");
    const std::string next_answer = Field(next.get(), "turn_id");
    assert(next_answer != answer);
    const auto before = test.network.sent_pcm.load();
    test.network.Feed({{"type", "listen.ack"}, {"turn_id", next_answer}, {"practice_id", kPracticeId}}, "accepted");
    WaitUntil([&] { return test.network.sent_pcm > before; });
    assert(test.client.Button());
    test.network.Take("listen.stop");
    test.StartReply(next_answer);
    WaitUntil([&] { return test.audio.playing.load(); });
    test.client.ExitPractice();
    assert(!test.audio.playing);
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    const auto exit = test.network.Take("scene.exit");
    const auto starts = test.audio.starts.load();
    test.StartReply(next_answer);
    test.network.Feed(2, "late-packet");
    const auto late = test.network.Take("delivery.ack");
    assert(std::string_view(Field(late.get(), "status")) == "aborted");
    assert(test.audio.starts == starts);
    test.network.Feed({{"type", "scene.ack"}, {"request_id", Field(exit.get(), "request_id")},
                       {"active_scene_id", kDailyScene}}, "ok");
    WaitUntil([&] { return test.client.state() == VoiceState::Ready; });
    assert(test.client.GetPracticeView().caption.empty());
    assert(!test.client.GetPracticeView().can_repeat);
    assert(test.client.Button());
    const auto daily = test.network.Take("listen.start");
    assert(std::string_view(Field(daily.get(), "scene_id")) == kDailyScene);
    std::puts("PASS practice opening, repeat, capture, immediately muted exit and stale reply fencing");
}

void RecoverFromMissingAcks() {
    TestDevice test;
    test.Hello(true);
    std::string request, opening;
    test.Enter(request, opening);
    test.network.extra_us += 11'000'000;
    test.Hello(true);
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    test.Enter(request, opening);
    test.network.Feed({{"type", "scene.ack"}, {"request_id", request}, {"active_scene_id", kEnglishScene},
                       {"practice_id", kPracticeId}, {"turn_id", opening}}, "ok");
    WaitUntil([&] { return test.client.GetPracticeView().page == PracticePage::Active; });
    test.client.ExitPractice();
    test.network.Take("scene.exit");
    test.network.extra_us += 11'000'000;
    test.Hello(true);
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    std::puts("PASS entry and exit timeout reconnect to daily");
}

void RejectedEntryAndDisconnect() {
    TestDevice test;
    test.Hello(true);
    std::string request, opening;
    test.Enter(request, opening);
    test.network.Feed({{"type", "scene.ack"}, {"request_id", request}, {"active_scene_id", kDailyScene}}, "ok", false);
    WaitUntil([&] { return test.client.state() == VoiceState::Ready; });
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    assert(test.audio.starts == 0);

    test.Enter(request, opening);
    test.network.Feed({{"type", "scene.ack"}, {"request_id", request}, {"active_scene_id", kEnglishScene},
                       {"practice_id", kPracticeId}, {"turn_id", opening}}, "ok");
    test.StartReply(opening);
    WaitUntil([&] { return test.audio.playing.load(); });
    test.network.Feed(0);
    WaitUntil([&] { return test.client.GetPracticeView().page == PracticePage::Offline; });
    assert(!test.audio.playing);
    test.Hello(false);
    assert(test.client.GetPracticeView().page == PracticePage::Available);
    std::puts("PASS rejected entry and disconnect stop audio and reset scene");
}

}  // namespace

int64_t esp_timer_get_time() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count() + wire->extra_us;
}

void esp_fill_random(void* destination, size_t size) {
    static std::atomic<uint8_t> seed{0};
    std::memset(destination, ++seed, size);
}

namespace agentsphere {

bool VoiceSocket::Open(const std::string&, const std::string&) {
    failed_ = false;
    connected_ = true;
    return true;
}
void VoiceSocket::Close() { connected_ = false; }
bool VoiceSocket::Send(const void* data, size_t size, bool binary) {
    std::lock_guard<std::mutex> lock(wire->guard);
    if (binary) ++wire->sent_pcm;
    else wire->outgoing.emplace_back(static_cast<const char*>(data), size);
    wire->changed.notify_all();
    return true;
}
VoiceSocket::Message* VoiceSocket::Receive(size_t* size, TickType_t wait) {
    std::unique_lock<std::mutex> lock(wire->guard);
    if (wait > 0) wire->changed.wait_for(lock, std::chrono::milliseconds(wait), [] { return !wire->incoming.empty(); });
    if (wire->incoming.empty()) return nullptr;
    auto [opcode, bytes] = std::move(wire->incoming.front());
    wire->incoming.pop_front();
    if (opcode == 0) {
        failed_ = true;
        return nullptr;
    }
    assert(bytes.size() <= sizeof(Message::data));
    *size = bytes.size();
    auto* message = new Message;
    message->opcode = opcode;
    std::memcpy(message->data, bytes.data(), *size);
    return message;
}
void VoiceSocket::Release(Message* message) { delete message; }

bool OpusFrames::OpenEncoder() {
    encoder_ = this;
    return true;
}
bool OpusFrames::OpenDecoder() {
    decoder_ = this;
    return true;
}
void OpusFrames::Close() { encoder_ = decoder_ = nullptr; }
unsigned OpusFrames::Encode() { return encoder_ ? 1 : 0; }
bool OpusFrames::Decode(uint8_t*, unsigned) { return decoder_ != nullptr; }

}  // namespace agentsphere

int main() {
    OldServer();
    DynamicEnableOnTap();
    HeadlessServer();
    PracticeRoundTrip();
    RecoverFromMissingAcks();
    RejectedEntryAndDisconnect();
}
