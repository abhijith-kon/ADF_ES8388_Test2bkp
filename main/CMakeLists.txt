idf_component_register(SRCS "main.c" "app_music.c" "app_files.c" "app_alarm.c" "ui.c" "home_rg_gui.c"
                    INCLUDE_DIRS "."
                    REQUIRES retro-go my_board esp_peripherals input_manager audio_pipeline audio_stream esp-adf-libs audio_hal)
