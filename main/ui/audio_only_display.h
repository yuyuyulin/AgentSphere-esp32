#pragma once

#include "ui/display.h"

namespace agentsphere {

class AudioOnlyDisplay final : public Display {
public:
    esp_err_t Init() override;
    void SetState(DeviceState state, const char* status_text = nullptr) override;
    void SetEmotion(Emotion emotion) override;
    void SetPowerStatus(const PowerStatus& power) override;
    void SetWifiStatus(bool connected, const char* ssid, const char* ip) override;
    void ShowProvisioning(const char* ap_ssid, const char* ip_addr) override;
    void ShowPairingCode(const char* code, const char* hint = nullptr) override;
    void SetDiagnosticTelemetry(const ImuData& imu,
                                const RtcTime& rtc,
                                const TouchPoint& touch,
                                const char* last_event) override;
    void SetBrightness(uint8_t percent) override;
    void SetScreenOn(bool on) override;
};

}  // namespace agentsphere
