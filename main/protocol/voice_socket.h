#pragma once

#include <atomic>
#include <string>

#include "esp_websocket_client.h"
#include "freertos/ringbuf.h"

namespace agentsphere {

// The IDF callback assembles complete bounded WebSocket messages. The voice
// worker is the sole consumer and owns all sends and connection destruction.
class VoiceSocket {
public:
    struct Message {
        uint8_t opcode;
        uint8_t data[8192];
    };

    bool Open(const std::string& url, const std::string& headers);
    void Close();
    bool Send(const void* data, size_t len, bool binary = false);
    Message* Receive(size_t* payload_len, TickType_t wait);
    void Release(Message* message);
    bool connected() const { return connected_; }
    bool failed() const { return failed_; }
    bool unauthorized() const { return unauthorized_; }

private:
    static void OnEvent(void* arg, esp_event_base_t, int32_t id, void* event);
    void Append(const esp_websocket_event_data_t& event);

    esp_websocket_client_handle_t client_ = nullptr;
    RingbufHandle_t received_ = nullptr;
    Message* partial_ = nullptr;
    size_t used_ = 0;
    bool assembling_ = false;
    std::atomic<bool> connected_{false}, failed_{false}, unauthorized_{false};
};

}  // namespace agentsphere
