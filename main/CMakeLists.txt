idf_component_register(SRCS "main.c" "app_music.c" "app_files.c" "app_alarm.c" "app_radio.c" "ui.c" "home_rg_gui.c" "game_sound.c" "game_tetris.c" "game_2048.c" "game_pong.c"
                    INCLUDE_DIRS "."
                    REQUIRES retro-go my_board esp_peripherals input_manager audio_pipeline audio_stream esp-adf-libs audio_hal app_update esp_partition)
