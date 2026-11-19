#include "sdkconfig.h"

#if CONFIG_AS_BOARD_GENERIC_HEADLESS

#include "boards/board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ui/audio_only_display.h"

namespace agentsphere {

namespace {
constexpr const char* kTag = "HeadlessBoard";

class GenericHeadlessBoard final : public Board {
public:
    const char* GetBoardName() const override { return "generic-headless-esp32s3"; }

    BoardCapabilities GetCapabilities() const override {
        BoardCapabilities caps{};
        caps.has_screen = false;
        caps.screen_desc = "none";
        return caps;
    }

    esp_err_t Init(QueueHandle_t /*event_queue*/) override {
        ESP_LOGI(kTag, "Initializing generic headless board");
        return display_.Init();
    }

    Display* GetDisplay() override { return &display_; }
    AudioCodec* GetAudioCodec() override { return nullptr; }
    PowerManager* GetPowerManager() override { return nullptr; }
    ImuSensor* GetImuSensor() override { return nullptr; }
    RtcClock* GetRtcClock() override { return nullptr; }

private:
    AudioOnlyDisplay display_;
};

}  // namespace

Board& Board::GetInstance() {
    static GenericHeadlessBoard instance;
    return instance;
}

}  // namespace agentsphere

#endif  // CONFIG_AS_BOARD_GENERIC_HEADLESS
