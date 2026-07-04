idf_component_register(
    SRCS "input_manager.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../my_board/my_board_v1_0"
    REQUIRES driver esp_timer
)