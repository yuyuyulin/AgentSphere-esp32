#pragma once
struct esp_app_desc_t { const char* version; };
inline const esp_app_desc_t* esp_app_get_description() {
    static constexpr esp_app_desc_t app{"host-fixture"};
    return &app;
}
