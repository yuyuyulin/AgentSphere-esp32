#pragma once

#include <cstdint>

#include "app/events.h"
#include "app/quick_controls.h"
#include "app/state_machine.h"
#include "esp_err.h"

namespace agentsphere {

class Display {
public:
    virtual ~Display() = default;

    virtual esp_err_t Init() = 0;
    virtual void SetState(DeviceState state, const char* status_text = nullptr) = 0;
    virtual void SetEmotion(Emotion emotion) = 0;
    virtual void SetPowerStatus(const PowerStatus& power) = 0;
    virtual void SetWifiStatus(bool connected, const char* ssid, const char* ip) = 0;
    virtual void ShowProvisioning(const char* ap_ssid, const char* ip_addr) = 0;
    virtual void ShowPairingCode(const char* code, const char* hint = nullptr) = 0;
    virtual void SetDiagnosticTelemetry(const ImuData& imu,
                                        const RtcTime& rtc,
                                        const TouchPoint& touch,
                                        const char* last_event) = 0;
    virtual void SetQuickControls(const QuickControls&) {}
    virtual void SetBrightness(uint8_t percent) = 0;
    virtual void SetScreenOn(bool on) = 0;
};

}  // namespace agentsphere
