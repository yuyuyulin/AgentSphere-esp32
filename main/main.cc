#include "app/application.h"
#include "esp_log.h"

extern "C" void app_main(void) {
    static agentsphere::Application app;
    const esp_err_t err = app.Init();
    if (err != ESP_OK) {
        ESP_LOGE("main", "Application initialization failed: %s", esp_err_to_name(err));
        return;
    }
    app.Run();
}
