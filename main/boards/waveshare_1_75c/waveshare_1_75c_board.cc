#include "sdkconfig.h"

#if CONFIG_AS_BOARD_WAVESHARE_1_75C

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

#include "boards/board.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/audio_only_display.h"

#if CONFIG_AS_ENABLE_LVGL
#include "ui/round_amoled/round_display.h"
#endif

namespace agentsphere {

namespace {

constexpr const char* kTag = "Board1.75C";
constexpr gpio_num_t kBootButtonGpio = GPIO_NUM_0;
constexpr uint32_t kI2cFreqHz = 400000;
constexpr int kAudioSampleRateHz = 16000;

constexpr uint8_t kAxp2101Addr = 0x34;
constexpr uint8_t kQmi8658AddrPrimary = 0x6B;
constexpr uint8_t kQmi8658AddrSecondary = 0x6A;
constexpr uint8_t kPcf85063Addr = 0x51;

esp_err_t I2cReadReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* data, size_t len) {
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 50);
}

esp_err_t I2cWriteReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), 50);
}

uint8_t BcdToDec(uint8_t v) { return static_cast<uint8_t>(((v >> 4) * 10) + (v & 0x0F)); }
uint8_t DecToBcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

void ScanI2cBus(i2c_master_bus_handle_t bus) {
    ESP_LOGI(kTag, "Scanning shared I2C bus (SCL=%d, SDA=%d)...", BSP_I2C_SCL, BSP_I2C_SDA);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (i2c_master_probe(bus, addr, 15) == ESP_OK) {
            ESP_LOGI(kTag, "  I2C device found at 0x%02X", addr);
            ++found;
        }
    }
    ESP_LOGI(kTag, "I2C scan complete: %d device(s) detected", found);
}

// ---------------------------------------------------------------------------
// ES7210 (Dual Mic) + ES8311 (Speaker) Audio Codec
// ---------------------------------------------------------------------------
class WaveshareAudioCodec final : public AudioCodec {
public:
    esp_err_t Init() override {
        i2s_std_config_t std_cfg{};
        std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRateHz);
        std_cfg.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        std_cfg.gpio_cfg.mclk = BSP_I2S_MCLK;
        std_cfg.gpio_cfg.bclk = BSP_I2S_SCLK;
        std_cfg.gpio_cfg.ws = BSP_I2S_LCLK;
        std_cfg.gpio_cfg.dout = BSP_I2S_DOUT;
        std_cfg.gpio_cfg.din = BSP_I2S_DSIN;

        esp_err_t err = bsp_audio_init(&std_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "bsp_audio_init failed: %s", esp_err_to_name(err));
            return err;
        }

        spk_dev_ = bsp_audio_codec_speaker_init();
        mic_dev_ = bsp_audio_codec_microphone_init();
        if (!spk_dev_ || !mic_dev_) {
            ESP_LOGE(kTag, "Codec init failed (spk=%p mic=%p)", spk_dev_, mic_dev_);
            return ESP_FAIL;
        }

        esp_codec_dev_sample_info_t fs{};
        fs.bits_per_sample = 16;
        fs.channel = 1;
        fs.channel_mask = 0;
        fs.sample_rate = kAudioSampleRateHz;
        fs.mclk_multiple = 0;

        err = esp_codec_dev_open(spk_dev_, &fs);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_codec_dev_open(spk) failed: %s", esp_err_to_name(err));
            return err;
        }
        esp_codec_dev_set_out_vol(spk_dev_, 70);

        err = esp_codec_dev_open(mic_dev_, &fs);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_codec_dev_open(mic) failed: %s", esp_err_to_name(err));
            return err;
        }
        esp_codec_dev_set_in_gain(mic_dev_, 24.0f);

        ready_ = true;
        ESP_LOGI(kTag, "Audio initialized (ES8311 + ES7210 @ %dHz 16-bit mono)", kAudioSampleRateHz);
        return ESP_OK;
    }

    bool IsReady() const override { return ready_; }

    esp_err_t SetOutputVolume(int volume_percent) override {
        if (!ready_) return ESP_ERR_INVALID_STATE;
        if (volume_percent < 0) volume_percent = 0;
        if (volume_percent > 100) volume_percent = 100;
        return esp_codec_dev_set_out_vol(spk_dev_, volume_percent);
    }

    esp_err_t PlayTone(int frequency_hz, int duration_ms) override {
        if (!ready_ || duration_ms <= 0 || frequency_hz <= 0) return ESP_ERR_INVALID_STATE;
        stop_requested_ = false;

        constexpr int kChunkSamples = 320;  // 20ms @ 16kHz
        int16_t chunk[kChunkSamples];
        const int total_samples = (kAudioSampleRateHz * duration_ms) / 1000;
        const float omega = 2.0f * 3.14159265f * static_cast<float>(frequency_hz) /
                            static_cast<float>(kAudioSampleRateHz);

        int written = 0;
        while (written < total_samples && !stop_requested_) {
            const int n = (total_samples - written) > kChunkSamples ? kChunkSamples : (total_samples - written);
            for (int i = 0; i < n; ++i) {
                const float sample = std::sin(omega * static_cast<float>(written + i));
                chunk[i] = static_cast<int16_t>(sample * 9000.0f);
            }
            esp_codec_dev_write(spk_dev_, chunk, n * static_cast<int>(sizeof(int16_t)));
            written += n;
        }
        return ESP_OK;
    }

    esp_err_t RecordAndLoopback(int duration_ms) override {
        if (!ready_ || duration_ms <= 0) return ESP_ERR_INVALID_STATE;
        stop_requested_ = false;

        const size_t total_samples = static_cast<size_t>((kAudioSampleRateHz * duration_ms) / 1000);
        const size_t total_bytes = total_samples * sizeof(int16_t);
        auto* pcm = static_cast<int16_t*>(
            heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!pcm) {
            ESP_LOGE(kTag, "Failed to allocate %u bytes in PSRAM for loopback",
                     static_cast<unsigned>(total_bytes));
            return ESP_ERR_NO_MEM;
        }

        ESP_LOGI(kTag, "Recording %d ms from ES7210...", duration_ms);
        const esp_err_t rerr = esp_codec_dev_read(mic_dev_, pcm, static_cast<int>(total_bytes));
        if (rerr != ESP_OK) {
            ESP_LOGE(kTag, "Mic read failed: %s", esp_err_to_name(rerr));
            heap_caps_free(pcm);
            return rerr;
        }

        // Compute peak and average amplitude for bring-up verification
        int32_t peak = 0;
        int64_t sum_abs = 0;
        for (size_t i = 0; i < total_samples; ++i) {
            const int32_t v = pcm[i] < 0 ? -static_cast<int32_t>(pcm[i]) : static_cast<int32_t>(pcm[i]);
            if (v > peak) peak = v;
            sum_abs += v;
        }
        const int32_t avg = static_cast<int32_t>(sum_abs / static_cast<int64_t>(total_samples));
        ESP_LOGI(kTag, "Mic capture complete (peak=%ld avg=%ld), playing back...",
                 static_cast<long>(peak), static_cast<long>(avg));

        constexpr size_t kChunkBytes = 640;  // 20ms chunks so StopOutput can interrupt immediately
        size_t offset = 0;
        while (offset < total_bytes && !stop_requested_) {
            const size_t n = (total_bytes - offset) > kChunkBytes ? kChunkBytes : (total_bytes - offset);
            esp_codec_dev_write(spk_dev_, reinterpret_cast<uint8_t*>(pcm) + offset, static_cast<int>(n));
            offset += n;
        }

        heap_caps_free(pcm);
        return ESP_OK;
    }

    esp_err_t StopOutput() override {
        stop_requested_ = true;
        return ESP_OK;
    }

private:
    esp_codec_dev_handle_t spk_dev_ = nullptr;
    esp_codec_dev_handle_t mic_dev_ = nullptr;
    bool ready_ = false;
    volatile bool stop_requested_ = false;
};

// ---------------------------------------------------------------------------
// AXP2101 Power Management IC (0x34) + PWRON Button IRQ
// ---------------------------------------------------------------------------
class Axp2101PowerManager final : public PowerManager {
public:
    explicit Axp2101PowerManager(i2c_master_bus_handle_t bus) : bus_(bus) {}

    esp_err_t Init() override {
        if (i2c_master_probe(bus_, kAxp2101Addr, 20) != ESP_OK) {
            ESP_LOGW(kTag, "AXP2101 not detected at 0x%02X", kAxp2101Addr);
            return ESP_ERR_NOT_FOUND;
        }

        i2c_device_config_t cfg{};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = kAxp2101Addr;
        cfg.scl_speed_hz = kI2cFreqHz;
        esp_err_t err = i2c_master_bus_add_device(bus_, &cfg, &dev_);
        if (err != ESP_OK) return err;

        uint8_t chip_id = 0;
        if (I2cReadReg(dev_, 0x03, &chip_id, 1) == ESP_OK) {
            ESP_LOGI(kTag, "AXP2101 detected (chip_id=0x%02X)", chip_id);
        }

        // Enable battery voltage ADC (reg 0x30 bit 0) and fuel gauge (reg 0x18 bit 3)
        uint8_t reg_val = 0;
        if (I2cReadReg(dev_, 0x30, &reg_val, 1) == ESP_OK) {
            I2cWriteReg(dev_, 0x30, static_cast<uint8_t>(reg_val | 0x01));
        }
        if (I2cReadReg(dev_, 0x18, &reg_val, 1) == ESP_OK) {
            I2cWriteReg(dev_, 0x18, static_cast<uint8_t>(reg_val | 0x08));
        }

        // Enable PWRON short (bit 3) and long (bit 2) press IRQs, clear pending flags
        if (I2cReadReg(dev_, 0x41, &reg_val, 1) == ESP_OK) {
            I2cWriteReg(dev_, 0x41, static_cast<uint8_t>(reg_val | 0x0C));
        }
        I2cWriteReg(dev_, 0x49, 0xFF);

        ready_ = true;
        return ESP_OK;
    }

    PowerStatus ReadStatus() override {
        PowerStatus st{};
        if (!ready_) return st;

        uint8_t stat1 = 0;
        uint8_t stat2 = 0;
        uint8_t vbat_raw[2] = {0, 0};
        uint8_t soc = 0;

        if (I2cReadReg(dev_, 0x00, &stat1, 1) != ESP_OK ||
            I2cReadReg(dev_, 0x01, &stat2, 1) != ESP_OK) {
            return st;
        }
        I2cReadReg(dev_, 0x34, vbat_raw, 2);
        I2cReadReg(dev_, 0xA4, &soc, 1);

        st.available = true;
        st.usb_plugged = (stat1 & 0x20) != 0;
        const uint8_t chg_dir = (stat2 >> 5) & 0x03;
        st.charging = (chg_dir == 0x01);
        st.battery_mv = ((static_cast<int>(vbat_raw[0] & 0x3F) << 8) | vbat_raw[1]);
        st.battery_percent = soc <= 100 ? static_cast<int>(soc) : -1;
        return st;
    }

    bool PollPowerButton(ButtonAction* out_action) {
        if (!ready_ || !out_action) return false;
        uint8_t irq1 = 0;
        if (I2cReadReg(dev_, 0x49, &irq1, 1) != ESP_OK || (irq1 & 0x0C) == 0) {
            return false;
        }
        I2cWriteReg(dev_, 0x49, static_cast<uint8_t>(irq1 & 0x0C));
        if (irq1 & 0x04) {
            *out_action = ButtonAction::LongPress;
            return true;
        }
        if (irq1 & 0x08) {
            *out_action = ButtonAction::ShortPress;
            return true;
        }
        return false;
    }

    esp_err_t PowerOff() override {
        if (!ready_) return ESP_ERR_INVALID_STATE;
        uint8_t val = 0;
        esp_err_t err = I2cReadReg(dev_, 0x10, &val, 1);
        if (err != ESP_OK) return err;
        ESP_LOGW(kTag, "Requesting AXP2101 soft power-off...");
        return I2cWriteReg(dev_, 0x10, static_cast<uint8_t>(val | 0x01));
    }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    bool ready_ = false;
};

// ---------------------------------------------------------------------------
// QMI8658 6-Axis IMU (0x6B / 0x6A)
// ---------------------------------------------------------------------------
class Qmi8658ImuSensor final : public ImuSensor {
public:
    explicit Qmi8658ImuSensor(i2c_master_bus_handle_t bus) : bus_(bus) {}

    esp_err_t Init() override {
        uint8_t addr = 0;
        if (i2c_master_probe(bus_, kQmi8658AddrPrimary, 20) == ESP_OK) {
            addr = kQmi8658AddrPrimary;
        } else if (i2c_master_probe(bus_, kQmi8658AddrSecondary, 20) == ESP_OK) {
            addr = kQmi8658AddrSecondary;
        } else {
            ESP_LOGW(kTag, "QMI8658 not detected at 0x6B/0x6A");
            return ESP_ERR_NOT_FOUND;
        }

        i2c_device_config_t cfg{};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = addr;
        cfg.scl_speed_hz = kI2cFreqHz;
        esp_err_t err = i2c_master_bus_add_device(bus_, &cfg, &dev_);
        if (err != ESP_OK) return err;

        uint8_t who = 0;
        if (I2cReadReg(dev_, 0x00, &who, 1) != ESP_OK || who != 0x05) {
            ESP_LOGW(kTag, "Unexpected QMI8658 WHO_AM_I=0x%02X", who);
            return ESP_ERR_INVALID_RESPONSE;
        }

        I2cWriteReg(dev_, 0x02, 0x60);  // CTRL1: auto-increment
        I2cWriteReg(dev_, 0x03, 0x25);  // CTRL2: Accel +-8g, 250Hz
        I2cWriteReg(dev_, 0x04, 0x55);  // CTRL3: Gyro +-512dps, 250Hz
        I2cWriteReg(dev_, 0x08, 0x03);  // CTRL7: Enable Accel + Gyro

        ready_ = true;
        ESP_LOGI(kTag, "QMI8658 IMU initialized at 0x%02X", addr);
        return ESP_OK;
    }

    ImuData Read() override {
        ImuData data{};
        if (!ready_) return data;

        uint8_t raw[12]{};
        if (I2cReadReg(dev_, 0x35, raw, sizeof(raw)) != ESP_OK) {
            return data;
        }

        auto to_i16 = [](uint8_t lo, uint8_t hi) -> int16_t {
            return static_cast<int16_t>((static_cast<uint16_t>(hi) << 8) | lo);
        };

        data.ax = static_cast<float>(to_i16(raw[0], raw[1])) / 4096.0f;
        data.ay = static_cast<float>(to_i16(raw[2], raw[3])) / 4096.0f;
        data.az = static_cast<float>(to_i16(raw[4], raw[5])) / 4096.0f;
        data.gx = static_cast<float>(to_i16(raw[6], raw[7])) / 64.0f;
        data.gy = static_cast<float>(to_i16(raw[8], raw[9])) / 64.0f;
        data.gz = static_cast<float>(to_i16(raw[10], raw[11])) / 64.0f;
        data.valid = true;
        return data;
    }

    ImuGesture DetectGesture(const ImuData& sample) override {
        if (!sample.valid) return ImuGesture::None;

        const float mag = std::sqrt(sample.ax * sample.ax + sample.ay * sample.ay + sample.az * sample.az);
        if (mag > 2.1f) {
            return ImuGesture::Shake;
        }
        if (!face_down_ && sample.az < -0.75f) {
            face_down_ = true;
            return ImuGesture::FlipFaceDown;
        }
        if (face_down_ && sample.az > 0.55f) {
            face_down_ = false;
            return ImuGesture::FaceUp;
        }
        return ImuGesture::None;
    }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    bool ready_ = false;
    bool face_down_ = false;
};

// ---------------------------------------------------------------------------
// PCF85063 RTC (0x51)
// ---------------------------------------------------------------------------
class Pcf85063RtcClock final : public RtcClock {
public:
    explicit Pcf85063RtcClock(i2c_master_bus_handle_t bus) : bus_(bus) {}

    esp_err_t Init() override {
        if (i2c_master_probe(bus_, kPcf85063Addr, 20) != ESP_OK) {
            ESP_LOGW(kTag, "PCF85063 RTC not detected at 0x%02X", kPcf85063Addr);
            return ESP_ERR_NOT_FOUND;
        }

        i2c_device_config_t cfg{};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = kPcf85063Addr;
        cfg.scl_speed_hz = kI2cFreqHz;
        esp_err_t err = i2c_master_bus_add_device(bus_, &cfg, &dev_);
        if (err != ESP_OK) return err;

        ready_ = true;
        ESP_LOGI(kTag, "PCF85063 RTC initialized at 0x%02X", kPcf85063Addr);
        return ESP_OK;
    }

    RtcTime ReadTime() override {
        RtcTime t{};
        if (!ready_) return t;

        uint8_t raw[7]{};
        if (I2cReadReg(dev_, 0x04, raw, sizeof(raw)) != ESP_OK) {
            return t;
        }

        t.second = BcdToDec(raw[0] & 0x7F);
        t.minute = BcdToDec(raw[1] & 0x7F);
        t.hour = BcdToDec(raw[2] & 0x3F);
        t.day = BcdToDec(raw[3] & 0x3F);
        t.month = BcdToDec(raw[5] & 0x1F);
        t.year = 2000 + BcdToDec(raw[6]);
        t.valid = true;
        return t;
    }

    esp_err_t SetTime(const RtcTime& time) override {
        if (!ready_) return ESP_ERR_INVALID_STATE;
        const uint8_t buf[8] = {
            0x04,
            DecToBcd(static_cast<uint8_t>(time.second % 60)),
            DecToBcd(static_cast<uint8_t>(time.minute % 60)),
            DecToBcd(static_cast<uint8_t>(time.hour % 24)),
            DecToBcd(static_cast<uint8_t>(time.day)),
            0,
            DecToBcd(static_cast<uint8_t>(time.month)),
            DecToBcd(static_cast<uint8_t>(time.year >= 2000 ? time.year - 2000 : 0)),
        };
        return i2c_master_transmit(dev_, buf, sizeof(buf), 50);
    }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    bool ready_ = false;
};

// ---------------------------------------------------------------------------
// Waveshare 1.75C Board Assembly
// ---------------------------------------------------------------------------
class Waveshare175CBoard final : public Board {
public:
    const char* GetBoardName() const override { return "waveshare-1.75c"; }

    BoardCapabilities GetCapabilities() const override {
        BoardCapabilities caps{};
        caps.has_screen = CONFIG_AS_ENABLE_LVGL;
        caps.screen_desc = CONFIG_AS_ENABLE_LVGL ? "466x466-round-amoled" : "none";
        caps.has_touch = CONFIG_AS_ENABLE_LVGL;
        caps.has_audio = true;
        caps.has_aec = true;
        caps.has_pmic = true;
        caps.has_imu = true;
        caps.has_rtc = true;
        return caps;
    }

    esp_err_t Init(QueueHandle_t event_queue) override {
        event_queue_ = event_queue;

        ESP_ERROR_CHECK(bsp_i2c_init());
        i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
        ScanI2cBus(i2c_bus);

#if CONFIG_AS_ENABLE_LVGL
        display_ = std::make_unique<RoundAmoledDisplay>(event_queue_);
#else
        display_ = std::make_unique<AudioOnlyDisplay>();
#endif
        esp_err_t err = display_->Init();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Display init failed: %s", esp_err_to_name(err));
            return err;
        }

        audio_ = std::make_unique<WaveshareAudioCodec>();
        if (audio_->Init() != ESP_OK) {
            ESP_LOGW(kTag, "Continuing bring-up without audio codec");
        }

        pmic_ = std::make_unique<Axp2101PowerManager>(i2c_bus);
        pmic_->Init();

        imu_ = std::make_unique<Qmi8658ImuSensor>(i2c_bus);
        imu_->Init();

        rtc_ = std::make_unique<Pcf85063RtcClock>(i2c_bus);
        rtc_->Init();

        InitBootButtonAndInputTask();
        return ESP_OK;
    }

    Display* GetDisplay() override { return display_.get(); }
    AudioCodec* GetAudioCodec() override { return audio_.get(); }
    PowerManager* GetPowerManager() override { return pmic_.get(); }
    ImuSensor* GetImuSensor() override { return imu_.get(); }
    RtcClock* GetRtcClock() override { return rtc_.get(); }

private:
    void InitBootButtonAndInputTask() {
        gpio_config_t io_cfg{};
        io_cfg.pin_bit_mask = (1ULL << kBootButtonGpio);
        io_cfg.mode = GPIO_MODE_INPUT;
        io_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_cfg.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_cfg);

        xTaskCreatePinnedToCore(
            &Waveshare175CBoard::InputPollTask, "input_poll", 3072, this, 4, nullptr, 0);
    }

    static void InputPollTask(void* arg) {
        auto* self = static_cast<Waveshare175CBoard*>(arg);
        bool boot_pressed = false;
        bool boot_long_sent = false;
        TickType_t press_start = 0;
        int pwr_poll_div = 0;

        while (true) {
            const bool low = (gpio_get_level(kBootButtonGpio) == 0);
            const TickType_t now = xTaskGetTickCount();

            if (low && !boot_pressed) {
                boot_pressed = true;
                boot_long_sent = false;
                press_start = now;
            } else if (low && boot_pressed && !boot_long_sent) {
                if ((now - press_start) >= pdMS_TO_TICKS(2500)) {
                    boot_long_sent = true;
                    AppEvent ev{};
                    ev.type = AppEventType::Button;
                    ev.button_id = ButtonId::Boot;
                    ev.button_action = ButtonAction::LongPress;
                    xQueueSend(self->event_queue_, &ev, 0);
                }
            } else if (!low && boot_pressed) {
                if (!boot_long_sent && (now - press_start) >= pdMS_TO_TICKS(40)) {
                    AppEvent ev{};
                    ev.type = AppEventType::Button;
                    ev.button_id = ButtonId::Boot;
                    ev.button_action = ButtonAction::ShortPress;
                    xQueueSend(self->event_queue_, &ev, 0);
                }
                boot_pressed = false;
            }

            if ((++pwr_poll_div % 3) == 0 && self->pmic_) {
                ButtonAction pwr_action{};
                if (self->pmic_->PollPowerButton(&pwr_action)) {
                    AppEvent ev{};
                    ev.type = AppEventType::Button;
                    ev.button_id = ButtonId::Power;
                    ev.button_action = pwr_action;
                    xQueueSend(self->event_queue_, &ev, 0);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    QueueHandle_t event_queue_ = nullptr;
    std::unique_ptr<Display> display_;
    std::unique_ptr<WaveshareAudioCodec> audio_;
    std::unique_ptr<Axp2101PowerManager> pmic_;
    std::unique_ptr<Qmi8658ImuSensor> imu_;
    std::unique_ptr<Pcf85063RtcClock> rtc_;
};

}  // namespace

Board& Board::GetInstance() {
    static Waveshare175CBoard instance;
    return instance;
}

}  // namespace agentsphere

#endif  // CONFIG_AS_BOARD_WAVESHARE_1_75C
