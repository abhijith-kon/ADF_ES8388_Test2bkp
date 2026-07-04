idf_component_register(SRCS "rg_display.c" "rg_audio.c" "rg_gui.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "../../components/my_board/my_board_v1_0"
                    REQUIRES audio_hal audio_sal driver esp_lcd espressif__esp_lcd_ili9341 esp_peripherals my_board)
