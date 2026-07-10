#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
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
#include "driver/ledc.h"

// Backlight config
#define DISPLAY_LED_PIN 11
static int current_brightness = 100;

void display_backlight_init(void) {
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 40000, // Move to 40kHz (inaudible) to prevent audio interference
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = 256, // 256 is 100% for 8-bit timer
        .gpio_num   = DISPLAY_LED_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);
}

void display_backlight_set(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    current_brightness = pct;
    int duty = (pct * 256) / 100; // 256 is exactly 100% DC
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Apps
#include "home_rg_gui.h"
#include "app_music.h"
#include "app_files.h"
#include "app_alarm.h"
#include "app_radio.h"
#include "ui.h" // Retro-OS games launcher
#include "app_audio_fx.h"
#include "app_wifi.h"
#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "es8388.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_periph_set_handle_t g_periph_set = NULL;
esp_periph_handle_t g_sdcard_handle = NULL;

void system_sdcard_suspend(void) {
    if (g_sdcard_handle) {
        esp_periph_stop(g_sdcard_handle);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_periph_remove_from_set(g_periph_set, g_sdcard_handle);
        esp_periph_destroy(g_sdcard_handle);
        g_sdcard_handle = NULL;
    }
}

void system_sdcard_resume(void) {
    if (!g_sdcard_handle && g_periph_set) {
        periph_sdcard_cfg_t sdcard_cfg = {
            .root = "/sdcard",
            .card_detect_pin = get_sdcard_intr_gpio(),
            .mode = SD_MODE_4_LINE,
        };
        g_sdcard_handle = periph_sdcard_init(&sdcard_cfg);
        esp_periph_start(g_periph_set, g_sdcard_handle);

        ESP_LOGI("MAIN", "Waiting for SD card...");
        int timeout = 50; 
        while (periph_sdcard_is_mounted(g_sdcard_handle) != true && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static const char *TAG = "MAIN";

int global_volume = 80;
static audio_hal_handle_t s_hal = NULL;

uint32_t g_system_boot_count = 0;
esp_reset_reason_t g_last_reset_reason = ESP_RST_UNKNOWN;
char g_sys_error_str[32] = "NONE";

void global_volume_set(int vol) {
    if (vol > 100) vol = 100;
    if (vol < 0) vol = 0;
    global_volume = vol;
    // Hardware volume is fixed to 100, volume is handled in software.
}

typedef enum {
    APP_HOME,
    APP_FILES,
    APP_MUSIC,
    APP_ALARM,
    APP_GAMES,
    APP_RADIO,
    APP_AUDIO_FX,
    APP_WIFI,
    APP_SETTINGS
} app_state_t;

static app_state_t current_app = APP_HOME;
static audio_board_handle_t board_handle = NULL;

#include <time.h>
#include <sys/time.h>
#include "i2c_bus.h"

#define DS3231_ADDR (0x68 << 1)

static uint8_t bcd2dec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }
static uint8_t dec2bcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }

static void rtc_set_time_ds3231(i2c_bus_handle_t bus, int wday, int year, int mon, int mday, int hour, int min, int sec) {
    uint8_t data[7];
    data[0] = dec2bcd(sec);
    data[1] = dec2bcd(min);
    data[2] = dec2bcd(hour);
    data[3] = dec2bcd(wday); // Day of week (1-7)
    data[4] = dec2bcd(mday);
    data[5] = dec2bcd(mon);
    data[6] = dec2bcd(year % 100);
    uint8_t reg = 0x00;
    esp_err_t err = i2c_bus_write_bytes(bus, DS3231_ADDR, &reg, 1, data, 7);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC DS3231 time initialized to %04d-%02d-%02d %02d:%02d:%02d", year, mon, mday, hour, min, sec);
    } else {
        ESP_LOGW(TAG, "Failed to write DS3231 time over i2c_bus (err=%d)", err);
    }
}

static void parse_compile_time(struct tm *build_tm) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char mstr[4] = {0};
    int mday = 0, year = 0, hour = 0, min = 0, sec = 0;
    sscanf(__DATE__, "%3s %d %d", mstr, &mday, &year);
    int mon = 0;
    for (int i = 0; i < 12; i++) {
        if (strcmp(mstr, months[i]) == 0) {
            mon = i;
            break;
        }
    }
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
    memset(build_tm, 0, sizeof(struct tm));
    build_tm->tm_year = year - 1900;
    build_tm->tm_mon = mon;
    build_tm->tm_mday = mday;
    build_tm->tm_hour = hour;
    build_tm->tm_min = min;
    build_tm->tm_sec = sec;
    mktime(build_tm); // computes tm_wday
}

static void rtc_sync_from_ds3231(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 4,
        .scl_io_num = 5,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_bus_handle_t bus = i2c_bus_create(I2C_NUM_0, &conf);
    if (!bus) {
        ESP_LOGE(TAG, "Failed to get i2c_bus handle for RTC");
        return;
    }
    uint8_t data[7] = {0};
    uint8_t reg = 0x00;
    esp_err_t err = i2c_bus_read_bytes(bus, DS3231_ADDR, &reg, 1, data, 7);
    
    struct tm build_tm;
    parse_compile_time(&build_tm);
    time_t build_time = mktime(&build_tm);

    if (err == ESP_OK) {
        int year = bcd2dec(data[6]) + 2000;
        int mon  = bcd2dec(data[5] & 0x1F);
        int mday = bcd2dec(data[4]);
        int hour = bcd2dec(data[2] & 0x3F);
        int min  = bcd2dec(data[1]);
        int sec  = bcd2dec(data[0] & 0x7F);

        struct tm rtc_tm = {
            .tm_sec  = sec,
            .tm_min  = min,
            .tm_hour = hour,
            .tm_mday = mday,
            .tm_mon  = mon - 1,
            .tm_year = year - 1900
        };
        time_t rtc_time = mktime(&rtc_tm);

        if (year < 2026 || rtc_time < build_time) {
            ESP_LOGW(TAG, "RTC DS3231 time (%04d-%02d-%02d %02d:%02d:%02d) is behind build time, updating to build time", year, mon, mday, hour, min, sec);
            int wday_ds = (build_tm.tm_wday + 1);
            rtc_set_time_ds3231(bus, wday_ds, build_tm.tm_year + 1900, build_tm.tm_mon + 1, build_tm.tm_mday, build_tm.tm_hour, build_tm.tm_min, build_tm.tm_sec);
            rtc_tm = build_tm;
        }

        struct timeval tv = { .tv_sec = mktime(&rtc_tm), .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "RTC DS3231 synced OK: %04d-%02d-%02d %02d:%02d:%02d", rtc_tm.tm_year + 1900, rtc_tm.tm_mon + 1, rtc_tm.tm_mday, rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
        
        // Ensure Oscillator runs on battery (clear EOSC in Control 0x0E) and clear OSF (Status 0x0F)
        uint8_t ctrl[2] = {0x00, 0x00}; // Reg 0x0E and 0x0F to 0
        reg = 0x0E;
        i2c_bus_write_bytes(bus, DS3231_ADDR, &reg, 1, ctrl, 2);
    } else {
        ESP_LOGW(TAG, "Failed to communicate with RTC DS3231 over i2c_bus (err=%d), setting system time to build time", err);
        strcpy(g_sys_error_str, "RTC_I2C_FAIL");
        struct timeval tv = { .tv_sec = build_time, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }
}

static void es8388_fix_output_mixer(void) {
    es8388_write_reg(ES8388_DACCONTROL17, 0x80);
    es8388_write_reg(ES8388_DACCONTROL20, 0x80);
    es8388_write_reg(ES8388_ADCPOWER, 0xFF);
    ESP_LOGI(TAG, "ES8388 mixer fixed");
}

void app_main(void)
{
    srand(esp_random());
    display_backlight_init();
    display_backlight_set(100); // Turn on screen immediately

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_last_reset_reason = esp_reset_reason();
    g_system_boot_count++;

    ESP_LOGI(TAG, "Starting Retro Console OS...");

    board_handle = audio_board_init();
    if (board_handle) {
        audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
        
        s_hal = board_handle->audio_hal;
        audio_hal_set_volume(s_hal, 70); // Lock hardware volume to 70 (new 100% limit)
        es8388_fix_output_mixer();
        rtc_sync_from_ds3231();
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
    // Draw random boot splash screen (240x320 with endian fix)
    #include "esp_random.h"
    #include "boot_images.h"
    uint16_t *splash_buf = malloc(240 * 2);
    if (splash_buf) {
        int r_idx = esp_random() % NUM_BOOT_IMAGES;
        const uint16_t *selected_img = boot_images[r_idx];
        for (int y = 0; y < 320; y++) {
            for (int x = 0; x < 240; x++) {
                uint16_t p = selected_img[y * 240 + x];
                splash_buf[x] = (p >> 8) | (p << 8); // Swap bytes for ILI9341
            }
            rg_display_write(0, y, 240, 1, 240 * 2, splash_buf);
        }
        free(splash_buf);
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Show for 1 second

    input_manager_init();

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    g_periph_set = esp_periph_set_init(&periph_cfg);

    system_sdcard_resume();
    
    // Init apps
    app_files_init();
    app_music_init(board_handle->audio_hal);
    app_alarm_init();
    app_audio_fx_init();
    app_wifi_init();
    app_settings_init();
    home_ui_init();

    ESP_LOGI(TAG, "System ready. Entering HOME.");
    
    static int64_t last_input_time = 0;
    static bool display_is_dimmed = false;

    while (1) {
        button_event_t event = input_manager_get_event();
        int64_t now_us = esp_timer_get_time();
        
        if (event != BTN_NONE) {
            last_input_time = now_us;
            if (display_is_dimmed) {
                display_is_dimmed = false;
                display_backlight_set(100);
                event = BTN_NONE; // Swallow event
            }
        } else {
            bool exclude_dim = (current_app == APP_GAMES) || 
                               (current_app == APP_FILES && app_files_is_in_rsvp_mode());
            if (!exclude_dim) {
                int seconds_idle = (now_us - last_input_time) / 1000000;
                if (seconds_idle >= 40) {
                    if (current_brightness > 0) {
                        display_backlight_set(0);
                        display_is_dimmed = true;
                    }
                } else if (seconds_idle >= 25) {
                    if (current_brightness > 20 && current_brightness != 0) { // Dim to 20%
                        display_backlight_set(20);
                        display_is_dimmed = true;
                    }
                }
            } else {
                last_input_time = now_us; // Keep resetting timer so it doesn't instantly sleep when exiting game
                if (display_is_dimmed) {
                    display_is_dimmed = false;
                    display_backlight_set(100);
                }
            }
        }
        
        // --- GLOBAL INPUT OVERRIDES ---
        if (event == BTN_VOL_UP || event == BTN_VOL_DOWN) {
            if (event == BTN_VOL_UP) {
                global_volume_set(global_volume + 10);
            } else {
                global_volume_set(global_volume - 10);
            }
            // Let the event fall through so apps (like music) can update their volume UI
        }

        // --- INPUT ROUTING ---
        if (current_app == APP_HOME) {
            if (event == BTN_LEFT) home_ui_move_left();
            else if (event == BTN_RIGHT) home_ui_move_right();
            else if (event == BTN_ENTER) {
                int selected = home_ui_get_selected();
                if (selected == 0) { // APP_FILES
                    current_app = APP_FILES;
                    app_files_start();
                }
                else if (selected == 1) { // APP_MUSIC
                    current_app = APP_MUSIC;
                    app_music_start();
                } 
                else if (selected == 2) { // APP_RADIO
                    current_app = APP_RADIO;
                    app_radio_start();
                }
                else if (selected == 3) { // APP_WIFI
                    current_app = APP_WIFI;
                    app_wifi_start();
                }
                else if (selected == 4) { // APP_AUDIO_FX
                    current_app = APP_AUDIO_FX;
                    app_audio_fx_start();
                }
                else if (selected == 5) { // APP_SETTINGS (formerly APP_VOL)
                    current_app = APP_SETTINGS;
                    app_settings_start();
                }
                else if (selected == 6) { // APP_ALARM
                    current_app = APP_ALARM;
                    app_alarm_start();
                }
                else if (selected == 7) { // APP_GAMES
                    current_app = APP_GAMES;
                    ui_init(); // Draw the retro-go launcher
                }
            }
        } 
        else if (current_app == APP_FILES) {
            if (event == BTN_ESCAPE && !app_files_is_in_page_view()) {
                app_files_stop();
                current_app = APP_HOME;
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                app_files_handle_input(event);
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
        else if (current_app == APP_ALARM) {
            if (event == BTN_ESCAPE) {
                app_alarm_stop();
                current_app = APP_HOME;
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                app_alarm_handle_input(event);
            }
        }
        else if (current_app == APP_GAMES) {
            if ((event == BTN_ESCAPE || event == BTN_B) && !ui_is_in_game()) {
                current_app = APP_HOME;
                // Drain any in-flight DMA from games app before clearing
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                ui_handle_input(event);
            }
        }
        else if (current_app == APP_RADIO) {
            if (event == BTN_ESCAPE) {
                app_radio_stop();
                current_app = APP_HOME;
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                app_radio_handle_input(event);
            }
        }
        else if (current_app == APP_WIFI) {
            if (event == BTN_ESCAPE) {
                app_wifi_stop();
                current_app = APP_HOME;
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                app_wifi_handle_input(event);
            }
        }
        else if (current_app == APP_AUDIO_FX) {
            if (event == BTN_ESCAPE) {
                app_audio_fx_stop();
                current_app = APP_HOME;
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                app_audio_fx_handle_input(event);
            }
        }
        else if (current_app == APP_SETTINGS) {
            if (event == BTN_ESCAPE) {
                app_settings_stop();
                current_app = APP_HOME;
                rg_display_drain();
                rg_gui_clear(0x0000);
                home_ui_force_redraw();
            } else {
                app_settings_handle_input(event);
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
        else if (current_app == APP_FILES) {
            app_files_tick();
        }
        else if (current_app == APP_ALARM) {
            // UI updates handled inside app_alarm_tick or input
        }
        else if (current_app == APP_GAMES) {
            ui_update();
        }
        else if (current_app == APP_RADIO) {
            app_radio_tick();
        }
        else if (current_app == APP_WIFI) {
            app_wifi_tick();
        }
        else if (current_app == APP_AUDIO_FX) {
            app_audio_fx_tick();
        }
        else if (current_app == APP_SETTINGS) {
            app_settings_tick();
        }

        app_alarm_tick(); // Check and ring alarm across all apps
        vTaskDelay(1);
    }
}
