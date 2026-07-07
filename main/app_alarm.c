#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "app_alarm.h"

static const char *TAG = "APP_ALARM";

#define SCREEN_W 240
#define SCREEN_H 320
#define BG_COLOR        RG_COLOR_RGB(0x10, 0x14, 0x24)
#define BOX_COLOR       RG_COLOR_RGB(0x1E, 0x25, 0x3E)
#define HIGHLIGHT_COLOR RG_COLOR_RGB(0x00, 0xC0, 0xFF)
#define TEXT_COLOR      RG_COLOR_WHITE

static bool in_ui = false;
static bool is_ringing = false;
static bool alarm_enabled = false;
static uint8_t alarm_hour = 7;
static uint8_t alarm_min = 0;
static uint8_t selected_field = 0; // 0: status, 1: hour, 2: minute
static bool alarm_triggered_today = false;
static int64_t last_ui_clock_update = 0;

static void save_alarm_settings(void)
{
    nvs_handle_t handle;
    if (nvs_open("alarm_store", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "enabled", alarm_enabled ? 1 : 0);
        nvs_set_u8(handle, "hour", alarm_hour);
        nvs_set_u8(handle, "min", alarm_min);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

static void load_alarm_settings(void)
{
    nvs_handle_t handle;
    if (nvs_open("alarm_store", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(handle, "enabled", &val) == ESP_OK) alarm_enabled = (val != 0);
        if (nvs_get_u8(handle, "hour", &val) == ESP_OK) alarm_hour = val % 24;
        if (nvs_get_u8(handle, "min", &val) == ESP_OK) alarm_min = val % 60;
        nvs_close(handle);
    }
}

static void set_buzzer(bool on)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, on ? 512 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void app_alarm_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 2700,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = GPIO_NUM_48,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    set_buzzer(false);
    load_alarm_settings();
    ESP_LOGI(TAG, "Alarm initialized: %02d:%02d [%s]", alarm_hour, alarm_min, alarm_enabled ? "ON" : "OFF");
}

static void draw_alarm_ui(void)
{
    if (!in_ui) return;
    rg_gui_draw_rect(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);

    // Header
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(HIGHLIGHT_COLOR);
    rg_gui_draw_text_box(0, 15, SCREEN_W, 25, BG_COLOR, "ALARM CLOCK");

    // Current time
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    char cur_str[32];
    snprintf(cur_str, sizeof(cur_str), "TIME: %02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    rg_gui_set_font_size(12);
    rg_gui_set_text_color(RG_COLOR_RGB(180, 180, 190));
    rg_gui_draw_text_box(0, 55, SCREEN_W, 20, BG_COLOR, cur_str);

    // Status box
    uint16_t box_bg = (selected_field == 0) ? HIGHLIGHT_COLOR : BOX_COLOR;
    uint16_t box_fg = (selected_field == 0) ? RG_COLOR_BLACK : TEXT_COLOR;
    char stat_str[32];
    snprintf(stat_str, sizeof(stat_str), "STATUS: [ %s ]", alarm_enabled ? "ON " : "OFF");
    rg_gui_draw_rect(30, 100, 180, 36, box_bg);
    rg_gui_set_font_size(12);
    rg_gui_set_text_color(box_fg);
    rg_gui_draw_text_box(30, 108, 180, 20, box_bg, stat_str);

    // Hour box
    box_bg = (selected_field == 1) ? HIGHLIGHT_COLOR : BOX_COLOR;
    box_fg = (selected_field == 1) ? RG_COLOR_BLACK : TEXT_COLOR;
    char hour_str[32];
    snprintf(hour_str, sizeof(hour_str), "HOUR:   [ %02d ]", alarm_hour);
    rg_gui_draw_rect(30, 150, 180, 36, box_bg);
    rg_gui_set_font_size(12);
    rg_gui_set_text_color(box_fg);
    rg_gui_draw_text_box(30, 158, 180, 20, box_bg, hour_str);

    // Minute box
    box_bg = (selected_field == 2) ? HIGHLIGHT_COLOR : BOX_COLOR;
    box_fg = (selected_field == 2) ? RG_COLOR_BLACK : TEXT_COLOR;
    char min_str[32];
    snprintf(min_str, sizeof(min_str), "MINUTE: [ %02d ]", alarm_min);
    rg_gui_draw_rect(30, 200, 180, 36, box_bg);
    rg_gui_set_font_size(12);
    rg_gui_set_text_color(box_fg);
    rg_gui_draw_text_box(30, 208, 180, 20, box_bg, min_str);

    // Footer instructions
    rg_gui_set_font_size(8);
    rg_gui_set_text_color(RG_COLOR_RGB(120, 130, 150));
    rg_gui_draw_text_box(0, 270, SCREEN_W, 15, BG_COLOR, "UP/DOWN: Adjust Value");
    rg_gui_draw_text_box(0, 285, SCREEN_W, 15, BG_COLOR, "LEFT/RIGHT: Select Field");
    rg_gui_draw_text_box(0, 300, SCREEN_W, 15, BG_COLOR, "ESC: Save & Return");

    rg_display_drain();
}

void app_alarm_start(void)
{
    in_ui = true;
    selected_field = 0;
    last_ui_clock_update = esp_timer_get_time() / 1000;
    rg_display_drain();
    draw_alarm_ui();
}

void app_alarm_stop(void)
{
    in_ui = false;
    save_alarm_settings();
}

bool app_alarm_is_in_ui(void)
{
    return in_ui;
}

bool app_alarm_is_ringing(void)
{
    return is_ringing;
}

void app_alarm_silence(void)
{
    if (is_ringing) {
        is_ringing = false;
        set_buzzer(false);
        ESP_LOGI(TAG, "Alarm silenced by user.");
    }
}

void app_alarm_handle_input(button_event_t event)
{
    if (is_ringing) {
        app_alarm_silence();
        return;
    }

    bool dirty = false;
    switch (event) {
        case BTN_LEFT:
            selected_field = (selected_field + 2) % 3;
            dirty = true;
            break;
        case BTN_RIGHT:
        case BTN_ENTER:
            selected_field = (selected_field + 1) % 3;
            dirty = true;
            break;
        case BTN_UP:
        case BTN_VOL_UP:
            if (selected_field == 0) {
                alarm_enabled = !alarm_enabled;
            } else if (selected_field == 1) {
                alarm_hour = (alarm_hour + 1) % 24;
            } else if (selected_field == 2) {
                alarm_min = (alarm_min + 1) % 60;
            }
            save_alarm_settings();
            dirty = true;
            break;
        case BTN_DOWN:
        case BTN_VOL_DOWN:
            if (selected_field == 0) {
                alarm_enabled = !alarm_enabled;
            } else if (selected_field == 1) {
                alarm_hour = (alarm_hour + 23) % 24;
            } else if (selected_field == 2) {
                alarm_min = (alarm_min + 59) % 60;
            }
            save_alarm_settings();
            dirty = true;
            break;
        case BTN_ESCAPE:
            app_alarm_stop();
            break;
        default:
            break;
    }

    if (dirty && in_ui) {
        draw_alarm_ui();
    }
}

void app_alarm_tick(void)
{
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    if (!t) return;

    if (alarm_enabled && t->tm_hour == alarm_hour && t->tm_min == alarm_min && t->tm_sec < 45) {
        if (!alarm_triggered_today) {
            is_ringing = true;
            alarm_triggered_today = true;
            ESP_LOGI(TAG, "ALARM RINGING! Time=%02d:%02d", alarm_hour, alarm_min);
        }
    } else if (t->tm_min != alarm_min) {
        alarm_triggered_today = false;
    }

    if (is_ringing) {
        static int64_t last_beep = 0;
        static bool beep_state = false;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_beep >= 200) {
            last_beep = now_ms;
            beep_state = !beep_state;
            set_buzzer(beep_state);
        }
    } else {
        set_buzzer(false);
    }

    if (in_ui) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_ui_clock_update >= 1000) {
            last_ui_clock_update = now_ms;
            char cur_str[32];
            snprintf(cur_str, sizeof(cur_str), "TIME: %02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            rg_gui_set_font_size(12);
            rg_gui_set_text_color(RG_COLOR_RGB(180, 180, 190));
            rg_gui_draw_text_box(0, 55, SCREEN_W, 20, BG_COLOR, cur_str);
        }
    }
}
