idf_component_register(
    SRCS
        "./my_board_v1_0/board.c"
        "./my_board_v1_0/board_pins_config.c"
        "./my_codec_driver/new_codec.c"
    INCLUDE_DIRS
        "./my_board_v1_0"
        "./my_codec_driver"
    REQUIRES audio_hal audio_sal driver esp_lcd esp_peripherals
    PRIV_REQUIRES display_service
)

# Explicitly add definitions for ADF drivers
target_compile_definitions(${COMPONENT_TARGET} PUBLIC "-DBOARD_PA_GAIN=10")
