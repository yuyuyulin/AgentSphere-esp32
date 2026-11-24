#pragma once

#include <cstddef>
#include <cstdint>

#include "app/events.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace agentsphere {

class Display;

struct BoardCapabilities {
    bool has_screen = false;
    const char* screen_desc = "none";
    bool has_touch = false;
    bool has_audio = false;
    bool has_aec = false;
    bool has_pmic = false;
    bool has_imu = false;
    bool has_rtc = false;
};

class AudioCodec {
public:
    virtual ~AudioCodec() = default;
    virtual esp_err_t Init() = 0;
    virtual bool IsReady() const = 0;
    virtual esp_err_t SetOutputVolume(int volume_percent) = 0;
    virtual esp_err_t PlayTone(int frequency_hz, int duration_ms) = 0;
    virtual esp_err_t RecordAndLoopback(int duration_ms) = 0;
    // Voice PCM is signed 16-bit, 16 kHz mono. BeginCapture discards samples
    // buffered before the user pressed BOOT. DrainOutput waits for hardware.
    virtual esp_err_t BeginCapture() = 0;
    virtual esp_err_t ReadPcm(int16_t* samples, size_t count) = 0;
    virtual esp_err_t BeginOutput() = 0;
    virtual esp_err_t WritePcm(int16_t* samples, size_t count) = 0;
    virtual esp_err_t DrainOutput() = 0;
    virtual esp_err_t StopOutput() = 0;
};

class PowerManager {
public:
    virtual ~PowerManager() = default;
    virtual esp_err_t Init() = 0;
    virtual PowerStatus ReadStatus() = 0;
    virtual esp_err_t PowerOff() = 0;
};

class ImuSensor {
public:
    virtual ~ImuSensor() = default;
    virtual esp_err_t Init() = 0;
    virtual ImuData Read() = 0;
    virtual ImuGesture DetectGesture(const ImuData& sample) = 0;
};

class RtcClock {
public:
    virtual ~RtcClock() = default;
    virtual esp_err_t Init() = 0;
    virtual RtcTime ReadTime() = 0;
    virtual esp_err_t SetTime(const RtcTime& time) = 0;
};

class Board {
public:
    virtual ~Board() = default;

    static Board& GetInstance();

    virtual const char* GetBoardName() const = 0;
    virtual BoardCapabilities GetCapabilities() const = 0;
    virtual esp_err_t Init(QueueHandle_t event_queue) = 0;

    virtual Display* GetDisplay() = 0;
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual PowerManager* GetPowerManager() = 0;
    virtual ImuSensor* GetImuSensor() = 0;
    virtual RtcClock* GetRtcClock() = 0;
};

}  // namespace agentsphere
