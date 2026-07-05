#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "input_manager.h"
#include "../components/my_board/my_board_v1_0/board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "board_pins_config.h"
#include "audio_hal.h"
#include "es8388.h"
#include "rg_display.h"
#include "rg_gui.h"

// Apps
#include "home_rg_gui.h"
#include "app_music.h"
#include "ui.h" // Retro-OS games launcher

static const char *TAG = "MAIN";

typedef enum {
    APP_HOME,
    APP_MUSIC,
    APP_GAMES
} app_state_t;

static app_state_t current_app = APP_HOME;
static audio_board_handle_t board_handle = NULL;

static void es8388_fix_output_mixer(void) {
    es8388_write_reg(ES8388_DACCONTROL17, 0x80);
    es8388_write_reg(ES8388_DACCONTROL20, 0x80);
    es8388_write_reg(ES8388_ADCPOWER, 0xFF);
    ESP_LOGI(TAG, "ES8388 mixer fixed");
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting Retro Console OS...");

    board_handle = audio_board_init();
    if (board_handle) {
        audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
        audio_hal_set_volume(board_handle->audio_hal, 80);
        es8388_fix_output_mixer();
    } else {
        ESP_LOGE(TAG, "Audio board init failed!");
    }

    rg_display_init();
    rg_display_config_t disp_conf = {
        .scaling = RG_SCREEN_SCALING_FIT,
        .rotation = RG_SCREEN_ROTATION_0, // Set portrait
        .filter = false
    };
    rg_display_set_config(disp_conf);
    input_manager_init();

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    periph_sdcard_cfg_t sdcard_cfg = {
        .root = "/sdcard",
        .card_detect_pin = get_sdcard_intr_gpio(),
        .mode = SD_MODE_4_LINE,
    };
    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    esp_periph_start(set, sdcard_handle);

    ESP_LOGI(TAG, "Waiting for SD card...");
    int timeout = 50; 
    while (periph_sdcard_is_mounted(sdcard_handle) != true && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Init apps
    app_music_init(board_handle->audio_hal);
    home_ui_init();

    ESP_LOGI(TAG, "System ready. Entering HOME.");
    
    while (1) {
        button_event_t event = input_manager_get_event();
        
        // --- INPUT ROUTING ---
        if (current_app == APP_HOME) {
            if (event == BTN_LEFT) home_ui_move_left();
            else if (event == BTN_RIGHT) home_ui_move_right();
            else if (event == BTN_ENTER) {
                int selected = home_ui_get_selected();
                if (selected == 1) { // APP_MUSIC
                    current_app = APP_MUSIC;
                    app_music_start();
                } 
                else if (selected == 7) { // APP_GAMES
                    current_app = APP_GAMES;
                    ui_init(); // Draw the retro-go launcher
                }
            }
        } 
        else if (current_app == APP_MUSIC) {
            if (event == BTN_ESCAPE && !app_music_is_in_player_ui()) {
                app_music_stop();
                current_app = APP_HOME;
                // Drain any in-flight DMA from music app before clearing
                rg_display_drain();
                rg_gui_clear(0x0000); 
                home_ui_force_redraw();
            } else {
                app_music_handle_input(event);
            }
        }
        else if (current_app == APP_GAMES) {
            if (event == BTN_ESCAPE || event == BTN_B) {
                current_app = APP_HOME;
                // Drain any in-flight DMA from games app before clearing
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                // Future: Send input to ui.c or active game
            }
        }

        // --- RENDER LOOP & TICK ---
        if (current_app == APP_HOME) {
            home_ui_tick();
            home_ui_update();
            home_ui_draw();
        } 
        else if (current_app == APP_MUSIC) {
            app_music_tick();
        }
        else if (current_app == APP_GAMES) {
            ui_update();
        }

        vTaskDelay(1);
    }
}
