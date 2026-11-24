#include "protocol/voice_socket.h"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace agentsphere {
namespace {
constexpr const char* kTag = "VoiceSocket";
constexpr int kNetworkTimeoutMs = 15000;
constexpr uint32_t kSendTimeoutMs = 2000;
}  // namespace

bool VoiceSocket::Open(const std::string& url, const std::string& headers) {
    connected_ = failed_ = unauthorized_ = false;
    used_ = 0;
    assembling_ = false;
    received_ = xRingbufferCreateWithCaps(64 * 1024, RINGBUF_TYPE_NOSPLIT,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    partial_ = static_cast<Message*>(heap_caps_malloc(sizeof(Message), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!received_ || !partial_) return false;

    esp_websocket_client_config_t cfg{};
    cfg.uri = url.c_str();
    cfg.headers = headers.c_str();
    cfg.transport = WEBSOCKET_TRANSPORT_OVER_SSL;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_reconnect = true;
    cfg.network_timeout_ms = kNetworkTimeoutMs;
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 15;
    cfg.keep_alive_interval = 5;
    cfg.keep_alive_count = 3;
    cfg.ping_interval_sec = 30;
    cfg.pingpong_timeout_sec = 30;
    cfg.buffer_size = 2048;
    client_ = esp_websocket_client_init(&cfg);
    return client_ &&
           esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, OnEvent, this) == ESP_OK &&
           esp_websocket_client_start(client_) == ESP_OK;
}

void VoiceSocket::Close() {
    if (client_) esp_websocket_client_destroy(client_);
    client_ = nullptr;
    if (received_) vRingbufferDeleteWithCaps(received_);
    received_ = nullptr;
    heap_caps_free(partial_);
    partial_ = nullptr;
    connected_ = false;
}

void VoiceSocket::OnEvent(void* arg, esp_event_base_t, int32_t id, void* event) {
    auto* self = static_cast<VoiceSocket*>(arg);
    auto* ev = static_cast<esp_websocket_event_data_t*>(event);
    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            self->connected_ = true;
            ESP_LOGI(kTag, "WSS connected");
            break;
        case WEBSOCKET_EVENT_DATA:
            if (!self->failed_) self->Append(*ev);
            break;
        case WEBSOCKET_EVENT_ERROR:
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_FINISH:
            if (ev && ev->error_handle.esp_ws_handshake_status_code == 401) self->unauthorized_ = true;
            if (!self->failed_.exchange(true)) {
                ESP_LOGW(kTag, "WSS link ended (event=%ld, ws_status=%d, err_type=%d, tls_err=0x%x, errno=%d)",
                         static_cast<long>(id),
                         ev ? ev->error_handle.esp_ws_handshake_status_code : 0,
                         ev ? static_cast<int>(ev->error_handle.error_type) : 0,
                         ev ? static_cast<unsigned>(ev->error_handle.esp_tls_last_esp_err) : 0u,
                         ev ? ev->error_handle.esp_transport_sock_errno : 0);
            }
            break;
        default:
            break;
    }
}

void VoiceSocket::Append(const esp_websocket_event_data_t& ev) {
    if (ev.op_code >= 8) return;  // Ping, pong and close are handled by IDF.
    if (ev.payload_offset == 0 && ev.op_code != 0) {
        if (assembling_ || (ev.op_code != 1 && ev.op_code != 2)) {
            failed_ = true;
            return;
        }
        assembling_ = true;
        partial_->opcode = ev.op_code;
        used_ = 0;
    }
    if (!assembling_ || ev.data_len < 0 || ev.payload_len < 0 || ev.payload_offset < 0 ||
        ev.data_len > ev.payload_len - ev.payload_offset ||
        static_cast<size_t>(ev.data_len) > sizeof(partial_->data) - used_) {
        failed_ = true;
        return;
    }
    if (ev.data_len) memcpy(partial_->data + used_, ev.data_ptr, ev.data_len);
    used_ += ev.data_len;
    if (ev.fin && ev.payload_offset + ev.data_len == ev.payload_len) {
        if (xRingbufferSend(received_, partial_, offsetof(Message, data) + used_, 0) != pdTRUE) {
            failed_ = true;  // Never acknowledge truncated playback.
        }
        assembling_ = false;
    }
}

VoiceSocket::Message* VoiceSocket::Receive(size_t* payload_len, TickType_t wait) {
    size_t size = 0;
    auto* msg = static_cast<Message*>(xRingbufferReceive(received_, &size, wait));
    if (msg) *payload_len = size - offsetof(Message, data);
    return msg;
}

void VoiceSocket::Release(Message* message) {
    vRingbufferReturnItem(received_, message);
}

bool VoiceSocket::Send(const void* data, size_t len, bool binary) {
    const auto* bytes = static_cast<const char*>(data);
    const TickType_t wait = pdMS_TO_TICKS(kSendTimeoutMs);
    if (failed_) return false;
    const int written = binary ? esp_websocket_client_send_bin(client_, bytes, len, wait)
                               : esp_websocket_client_send_text(client_, bytes, len, wait);
    if (written != static_cast<int>(len)) {
        if (!failed_.exchange(true)) {
            ESP_LOGW(kTag, "WSS %s send failed (written=%d expected=%u)",
                     binary ? "bin" : "text", written, static_cast<unsigned>(len));
        }
    }
    return !failed_;
}

}  // namespace agentsphere
