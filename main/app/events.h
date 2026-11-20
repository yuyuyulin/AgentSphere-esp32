#pragma once

#include <cstdint>

namespace agentsphere {

enum class ButtonId : uint8_t {
    Power = 0,
    Boot,
};

enum class ButtonAction : uint8_t {
    ShortPress = 0,
    LongPress,
    HoldOnBoot,
};

enum class ImuGesture : uint8_t {
    None = 0,
    PickUp,
    Shake,
    FlipFaceDown,
    FaceUp,
};

struct PowerStatus {
    bool available = false;
    bool usb_plugged = false;
    bool charging = false;
    int battery_percent = -1;
    int battery_mv = 0;
};

struct ImuData {
    bool valid = false;
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
};

struct RtcTime {
    bool valid = false;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct TouchPoint {
    bool pressed = false;
    int16_t x = 0;
    int16_t y = 0;
};

enum class AppEventType : uint8_t {
    Button = 0,
    Touch,
    ImuGesture,
    PowerChanged,
    Tick1Hz,
    AudioTestTrigger,
    WifiConnected,
    WifiDisconnected,
    WifiConnectTimeout,
    ProvisioningSkipped,
    ProvisioningConnectRequested,
};

struct AppEvent {
    AppEventType type = AppEventType::Tick1Hz;
    ButtonId button_id = ButtonId::Boot;
    ButtonAction button_action = ButtonAction::ShortPress;
    ImuGesture imu_gesture = ImuGesture::None;
    TouchPoint touch{};
    PowerStatus power{};
    char ssid[33]{};
    char ip[16]{};
};

}  // namespace agentsphere
