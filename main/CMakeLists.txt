set(APP_SRCS
    "main.cc"
    "app/application.cc"
    "app/state_machine.cc"
    "storage/settings_store.cc"
    "net/wifi_manager.cc"
    "net/provisioning.cc"
    "protocol/bootstrap_client.cc"
    "ui/audio_only_display.cc"
)

if(CONFIG_AS_BOARD_WAVESHARE_1_75C)
    list(APPEND APP_SRCS "boards/waveshare_1_75c/waveshare_1_75c_board.cc")
endif()

if(CONFIG_AS_BOARD_GENERIC_HEADLESS)
    list(APPEND APP_SRCS "boards/generic_headless/generic_headless_board.cc")
endif()

if(CONFIG_AS_ENABLE_LVGL)
    list(APPEND APP_SRCS
        "ui/fonts/font_zh_18.c"
        "ui/round_amoled/round_display.cc"
        "ui/round_amoled/theme_renderer.cc"
    )
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "."
)
