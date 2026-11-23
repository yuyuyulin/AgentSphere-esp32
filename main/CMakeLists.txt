set(APP_SRCS
    "main.cc"
    "app/application.cc"
    "app/state_machine.cc"
    "storage/settings_store.cc"
    "net/wifi_manager.cc"
    "net/provisioning.cc"
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
        "ui/round_amoled/round_display.cc"
        "ui/round_amoled/theme_renderer.cc"
        "ui/round_amoled/xiaoan_avatar.cc"
    )
    set(APP_ASSETS "ui/round_amoled/assets/xiaoan_sprites.rgb565")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APP_ASSETS}")
    file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/${APP_ASSETS}" XIAOAN_ASSET_BYTES)
    math(EXPR XIAOAN_EXPECTED_BYTES "6 * 224 * 188 * 2")
    if(NOT XIAOAN_ASSET_BYTES EQUAL XIAOAN_EXPECTED_BYTES)
        message(FATAL_ERROR "XiaoAn artwork has an invalid frame count or dimensions")
    endif()
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "."
    EMBED_FILES ${APP_ASSETS}
)
