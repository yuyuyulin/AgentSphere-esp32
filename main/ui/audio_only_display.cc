#include "ui/audio_only_display.h"

#include "esp_log.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "AudioOnlyDisplay";
}

esp_err_t AudioOnlyDisplay::Init() {
    ESP_LOGI(kTag, "Initialized headless audio-only display");
    return ESP_OK;
}

void AudioOnlyDisplay::SetState(DeviceState state, const char* status_text) {
    ESP_LOGI(kTag, "State=%s%s%s",
             DeviceStateToString(state),
             status_text ? " | " : "",
             status_text ? status_text : "");
}

void AudioOnlyDisplay::SetEmotion(Emotion emotion) {
    ESP_LOGD(kTag, "Emotion=%s", EmotionToString(emotion));
}

void AudioOnlyDisplay::SetPowerStatus(const PowerStatus& power) {
    if (power.available) {
        ESP_LOGD(kTag, "Power: usb=%d chg=%d soc=%d%% mv=%d",
                 power.usb_plugged, power.charging, power.battery_percent, power.battery_mv);
    }
}

void AudioOnlyDisplay::ShowPairingCode(const char* code) {
    ESP_LOGI(kTag, "Pairing code (headless voice prompt placeholder): %s", code ? code : "(null)");
}

void AudioOnlyDisplay::SetDiagnosticTelemetry(const ImuData& imu,
                                              const RtcTime& rtc,
                                              const TouchPoint& /*touch*/,
                                              const char* last_event) {
    ESP_LOGI(kTag, "Diag: imu(%d: %.2f,%.2f,%.2f) rtc(%d: %04d-%02d-%02d %02d:%02d:%02d) ev=%s",
             imu.valid, imu.ax, imu.ay, imu.az,
             rtc.valid, rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second,
             last_event ? last_event : "-");
}

void AudioOnlyDisplay::SetBrightness(uint8_t /*percent*/) {}
void AudioOnlyDisplay::SetScreenOn(bool /*on*/) {}

}  // namespace agentsphere
