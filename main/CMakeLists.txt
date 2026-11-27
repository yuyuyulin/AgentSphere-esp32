set(APP_SRCS
    "main.cc"
    "app/application.cc"
    "app/state_machine.cc"
    "storage/settings_store.cc"
    "net/wifi_manager.cc"
    "net/provisioning.cc"
    "protocol/bootstrap_client.cc"
    "protocol/voice_client.cc"
    "protocol/voice_socket.cc"
    "audio/opus_frames.cc"
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
        "ui/fonts/font_practice_zh_16.c"
        "ui/round_amoled/round_display.cc"
        "ui/round_amoled/theme_renderer.cc"
        "ui/round_amoled/xiaoan_avatar.cc"
    )
    set(APP_ASSETS "ui/round_amoled/assets/xiaoan_rig.rgb565a8")
    set(XIAOAN_INDEX "ui/round_amoled/xiaoan_rig_assets.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APP_ASSETS}" "${XIAOAN_INDEX}")
    file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/${APP_ASSETS}" XIAOAN_ASSET_BYTES)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${XIAOAN_INDEX}" XIAOAN_DATA_SIZE
         REGEX "kRigDataBytes = [0-9]+")
    string(REGEX MATCH "= ([0-9]+)" XIAOAN_SIZE_FIELD "${XIAOAN_DATA_SIZE}")
    set(XIAOAN_EXPECTED_BYTES "${CMAKE_MATCH_1}")
    if(NOT XIAOAN_EXPECTED_BYTES OR NOT XIAOAN_ASSET_BYTES EQUAL XIAOAN_EXPECTED_BYTES)
        message(FATAL_ERROR "XiaoAn rig data does not match its generated index")
    endif()
    list(APPEND APP_ASSETS "ui/fonts/practice_zh_bitmap.a2")
endif()

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "."
    EMBED_FILES ${APP_ASSETS}
)
