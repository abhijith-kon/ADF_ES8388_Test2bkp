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

enum { MODE_MENU, MODE_ALARM, MODE_STOPWATCH, MODE_TIMER };
static int app_mode = MODE_MENU;
static int menu_sel = 0;

static bool force_full_redraw = true;

static bool in_ui = false;
static bool is_ringing = false;
static bool alarm_enabled = false;
static uint8_t alarm_hour = 7;
static uint8_t alarm_min = 0;
static uint8_t selected_field = 0; 
static bool alarm_triggered_today = false;

static bool sw_running = false;
static int64_t sw_start_time = 0;
static int64_t sw_accumulated = 0;
static int64_t sw_lap_time = 0;
static bool sw_has_lap = false;

static bool tmr_running = false;
static int tmr_hours = 0;
static int tmr_mins = 0;
static int tmr_secs = 0;
static int tmr_sel = 0; 
static int64_t tmr_end_time = 0;
static bool tmr_ringing = false;

static bool buzzer_is_on = false;

static void set_buzzer(bool on) {
    if (on == buzzer_is_on) return;
    buzzer_is_on = on;
    if (on) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = LEDC_CHANNEL_1,
            .timer_sel      = LEDC_TIMER_1,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = GPIO_NUM_48,
            .duty           = 512,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_channel);
    } else {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
        gpio_reset_pin(GPIO_NUM_48);
        gpio_set_direction(GPIO_NUM_48, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_48, 0);
    }
}

void app_alarm_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 2700,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);
    buzzer_is_on = true; // force an update
    set_buzzer(false);
    ESP_LOGI(TAG, "Clock initialized");
}

static void draw_menu_ui(void) {
    if (force_full_redraw) {
        rg_gui_draw_rect(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);
        rg_gui_set_font_size(16);
        rg_gui_set_text_color(HIGHLIGHT_COLOR);
        rg_gui_draw_text_box(0, 15, SCREEN_W, 25, BG_COLOR, "CLOCK APPS");
    }
    const char *opts[] = {"CLOCK", "STOPWATCH", "TIMER"};
    for (int i = 0; i < 3; i++) {
        uint16_t box_bg = (menu_sel == i) ? HIGHLIGHT_COLOR : BOX_COLOR;
        uint16_t box_fg = (menu_sel == i) ? RG_COLOR_BLACK : TEXT_COLOR;
        rg_gui_draw_rect(40, 80 + i*50, 160, 36, box_bg);
        rg_gui_set_font_size(12);
        rg_gui_set_text_color(box_fg);
        rg_gui_draw_text_box(40, 88 + i*50, 160, 20, box_bg, opts[i]);
    }
    rg_display_drain();
}

static void draw_alarm_ui(void) {
    if (force_full_redraw) {
        rg_gui_draw_rect(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);
        rg_gui_set_font_size(16);
        rg_gui_set_text_color(HIGHLIGHT_COLOR);
        rg_gui_draw_text_box(0, 15, SCREEN_W, 25, BG_COLOR, "CLOCK");
    }

    time_t now; time(&now);
    struct tm *t = localtime(&now);
    char cur_str[32];
    snprintf(cur_str, sizeof(cur_str), "TIME: %02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    rg_gui_set_font_size(12);
    rg_gui_set_text_color(RG_COLOR_RGB(180, 180, 190));
    rg_gui_draw_text_box(0, 55, SCREEN_W, 20, BG_COLOR, cur_str);

    uint16_t box_bg = (selected_field == 0) ? HIGHLIGHT_COLOR : BOX_COLOR;
    uint16_t box_fg = (selected_field == 0) ? RG_COLOR_BLACK : TEXT_COLOR;
    char stat_str[32]; snprintf(stat_str, sizeof(stat_str), "STATUS: [ %s ]", alarm_enabled ? "ON " : "OFF");
    rg_gui_draw_rect(30, 100, 180, 36, box_bg);
    rg_gui_set_font_size(12); rg_gui_set_text_color(box_fg);
    rg_gui_draw_text_box(30, 108, 180, 20, box_bg, stat_str);

    box_bg = (selected_field == 1) ? HIGHLIGHT_COLOR : BOX_COLOR;
    box_fg = (selected_field == 1) ? RG_COLOR_BLACK : TEXT_COLOR;
    char hour_str[32]; snprintf(hour_str, sizeof(hour_str), "HOUR:   [ %02d ]", alarm_hour);
    rg_gui_draw_rect(30, 150, 180, 36, box_bg);
    rg_gui_set_font_size(12); rg_gui_set_text_color(box_fg);
    rg_gui_draw_text_box(30, 158, 180, 20, box_bg, hour_str);

    box_bg = (selected_field == 2) ? HIGHLIGHT_COLOR : BOX_COLOR;
    box_fg = (selected_field == 2) ? RG_COLOR_BLACK : TEXT_COLOR;
    char min_str[32]; snprintf(min_str, sizeof(min_str), "MINUTE: [ %02d ]", alarm_min);
    rg_gui_draw_rect(30, 200, 180, 36, box_bg);
    rg_gui_set_font_size(12); rg_gui_set_text_color(box_fg);
    rg_gui_draw_text_box(30, 208, 180, 20, box_bg, min_str);
    rg_display_drain();
}

static void draw_stopwatch_ui(void) {
    if (force_full_redraw) {
        rg_gui_draw_rect(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);
        rg_gui_set_font_size(16);
        rg_gui_set_text_color(HIGHLIGHT_COLOR);
        rg_gui_draw_text_box(0, 15, SCREEN_W, 25, BG_COLOR, "STOPWATCH");
        
        rg_gui_set_font_size(10);
        rg_gui_set_text_color(HIGHLIGHT_COLOR);
        rg_gui_draw_text_box(0, 280, SCREEN_W, 20, BG_COLOR, "[A] START/STOP  [B] LAP  [DOWN] RESET");
    }

    int64_t elapsed = sw_accumulated;
    if (sw_running) elapsed += (esp_timer_get_time() - sw_start_time);
    
    int ms = (elapsed / 10000) % 100;
    int s = (elapsed / 1000000) % 60;
    int m = (elapsed / 60000000) % 60;

    char sw_str[32];
    snprintf(sw_str, sizeof(sw_str), "%02d:%02d.%02d", m, s, ms);
    rg_gui_set_font_size(24);
    rg_gui_set_text_color(TEXT_COLOR);
    rg_gui_draw_text_box(0, 80, SCREEN_W, 40, BG_COLOR, sw_str);

    if (sw_has_lap) {
        int l_ms = (sw_lap_time / 10000) % 100;
        int l_s = (sw_lap_time / 1000000) % 60;
        int l_m = (sw_lap_time / 60000000) % 60;
        char lap_str[32];
        snprintf(lap_str, sizeof(lap_str), "LAP: %02d:%02d.%02d", l_m, l_s, l_ms);
        rg_gui_set_font_size(12);
        rg_gui_set_text_color(RG_COLOR_RGB(150, 150, 150));
        rg_gui_draw_text_box(0, 140, SCREEN_W, 20, BG_COLOR, lap_str);
    } else if (force_full_redraw) {
        rg_gui_draw_text_box(0, 140, SCREEN_W, 20, BG_COLOR, "");
    }

    rg_display_drain();
}

static void draw_timer_ui(void) {
    if (force_full_redraw) {
        rg_gui_draw_rect(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);
        rg_gui_set_font_size(16);
        rg_gui_set_text_color(HIGHLIGHT_COLOR);
        rg_gui_draw_text_box(0, 15, SCREEN_W, 25, BG_COLOR, "TIMER");
        
        rg_gui_set_font_size(24);
        rg_gui_set_text_color(TEXT_COLOR);
        rg_gui_draw_text_box(80, 80, 20, 40, BG_COLOR, ":");
        rg_gui_draw_text_box(160, 80, 20, 40, BG_COLOR, ":");
        
        rg_gui_set_font_size(10);
        rg_gui_set_text_color(HIGHLIGHT_COLOR);
        rg_gui_draw_text_box(0, 280, SCREEN_W, 20, BG_COLOR, "[ENTER] START/STOP  [ESC] BACK");
    }

    int h = tmr_hours, m = tmr_mins, s = tmr_secs;
    if (tmr_running) {
        int64_t rem = tmr_end_time - esp_timer_get_time();
        if (rem < 0) rem = 0;
        s = (rem / 1000000) % 60;
        m = (rem / 60000000) % 60;
        h = (rem / 3600000000LL);
    }

    uint16_t hb = (tmr_sel == 0) ? HIGHLIGHT_COLOR : TEXT_COLOR;
    uint16_t mb = (tmr_sel == 1) ? HIGHLIGHT_COLOR : TEXT_COLOR;
    uint16_t sb = (tmr_sel == 2) ? HIGHLIGHT_COLOR : TEXT_COLOR;
    
    char ts[32];
    rg_gui_set_font_size(24);
    
    snprintf(ts, sizeof(ts), "%02d", h);
    rg_gui_set_text_color(hb);
    rg_gui_draw_text_box(20, 80, 60, 40, BG_COLOR, ts);
    
    snprintf(ts, sizeof(ts), "%02d", m);
    rg_gui_set_text_color(mb);
    rg_gui_draw_text_box(100, 80, 60, 40, BG_COLOR, ts);

    snprintf(ts, sizeof(ts), "%02d", s);
    rg_gui_set_text_color(sb);
    rg_gui_draw_text_box(180, 80, 60, 40, BG_COLOR, ts);

    rg_display_drain();
}

static void draw_current_ui(void) {
    if (!in_ui) return;
    if (app_mode == MODE_MENU) draw_menu_ui();
    else if (app_mode == MODE_ALARM) draw_alarm_ui();
    else if (app_mode == MODE_STOPWATCH) draw_stopwatch_ui();
    else if (app_mode == MODE_TIMER) draw_timer_ui();
    force_full_redraw = false;
}

void app_alarm_start(void) {
    in_ui = true;
    app_mode = MODE_MENU;
    menu_sel = 0;
    force_full_redraw = true;
    draw_current_ui();
}

void app_alarm_stop(void) {
    in_ui = false;
}

bool app_alarm_is_in_ui(void) {
    return in_ui;
}

bool app_alarm_is_ringing(void) {
    return is_ringing || tmr_ringing;
}

void app_alarm_silence(void) {
    if (is_ringing) {
        is_ringing = false;
        set_buzzer(false);
        ESP_LOGI(TAG, "Alarm silenced.");
    }
    if (tmr_ringing) {
        tmr_ringing = false;
        set_buzzer(false);
        ESP_LOGI(TAG, "Timer silenced.");
    }
}

void app_alarm_handle_input(button_event_t event) {
    if (event == BTN_NONE) return; 
    if (app_alarm_is_ringing()) {
        app_alarm_silence();
        return;
    }
    bool dirty = false;
    if (app_mode == MODE_MENU) {
        if (event == BTN_DOWN) { menu_sel = (menu_sel + 1) % 3; dirty = true; }
        else if (event == BTN_UP) { menu_sel = (menu_sel + 2) % 3; dirty = true; }
        else if (event == BTN_A || event == BTN_ENTER) {
            if (menu_sel == 0) { app_mode = MODE_ALARM; selected_field = 0; force_full_redraw = true; }
            else if (menu_sel == 1) { app_mode = MODE_STOPWATCH; force_full_redraw = true; }
            else if (menu_sel == 2) { app_mode = MODE_TIMER; tmr_sel = 0; force_full_redraw = true; }
            dirty = true;
        }
        else if (event == BTN_ESCAPE) { app_alarm_stop(); }
    } else if (app_mode == MODE_ALARM) {
        if (event == BTN_ESCAPE) { app_mode = MODE_MENU; force_full_redraw = true; dirty = true; }
        else if (event == BTN_LEFT) { selected_field = (selected_field + 2) % 3; dirty = true; }
        else if (event == BTN_RIGHT || event == BTN_ENTER) { selected_field = (selected_field + 1) % 3; dirty = true; }
        else if (event == BTN_UP) {
            if (selected_field == 0) alarm_enabled = !alarm_enabled;
            else if (selected_field == 1) alarm_hour = (alarm_hour + 1) % 24;
            else if (selected_field == 2) alarm_min = (alarm_min + 1) % 60;
            dirty = true;
        }
        else if (event == BTN_DOWN) {
            if (selected_field == 0) alarm_enabled = !alarm_enabled;
            else if (selected_field == 1) alarm_hour = (alarm_hour + 23) % 24;
            else if (selected_field == 2) alarm_min = (alarm_min + 59) % 60;
            dirty = true;
        }
    } else if (app_mode == MODE_STOPWATCH) {
        if (event == BTN_ESCAPE) { app_mode = MODE_MENU; force_full_redraw = true; dirty = true; }
        else if (event == BTN_A || event == BTN_ENTER) {
            if (sw_running) {
                sw_accumulated += (esp_timer_get_time() - sw_start_time);
                sw_running = false;
            } else {
                sw_start_time = esp_timer_get_time();
                sw_running = true;
            }
            dirty = true;
        } else if (event == BTN_B) {
            if (sw_running) {
                sw_lap_time = sw_accumulated + (esp_timer_get_time() - sw_start_time);
                sw_has_lap = true;
                dirty = true;
            }
        } else if (event == BTN_DOWN) {
            sw_running = false;
            sw_accumulated = 0;
            sw_has_lap = false;
            force_full_redraw = true;
            dirty = true;
        }
    } else if (app_mode == MODE_TIMER) {
        if (event == BTN_ESCAPE) { app_mode = MODE_MENU; force_full_redraw = true; dirty = true; }
        else if (event == BTN_LEFT) { tmr_sel = (tmr_sel + 2) % 3; dirty = true; }
        else if (event == BTN_RIGHT) { tmr_sel = (tmr_sel + 1) % 3; dirty = true; }
        else if (event == BTN_UP && !tmr_running) {
            if (tmr_sel == 0) tmr_hours = (tmr_hours + 1) % 24;
            else if (tmr_sel == 1) tmr_mins = (tmr_mins + 1) % 60;
            else if (tmr_sel == 2) tmr_secs = (tmr_secs + 1) % 60;
            dirty = true;
        }
        else if (event == BTN_DOWN && !tmr_running) {
            if (tmr_sel == 0) tmr_hours = (tmr_hours + 23) % 24;
            else if (tmr_sel == 1) tmr_mins = (tmr_mins + 59) % 60;
            else if (tmr_sel == 2) tmr_secs = (tmr_secs + 59) % 60;
            dirty = true;
        }
        else if (event == BTN_ENTER) {
            if (tmr_running) {
                tmr_running = false;
                int64_t rem = tmr_end_time - esp_timer_get_time();
                if (rem < 0) rem = 0;
                tmr_secs = (rem / 1000000) % 60;
                tmr_mins = (rem / 60000000) % 60;
                tmr_hours = (rem / 3600000000LL);
            } else {
                int64_t total_us = (tmr_hours * 3600LL + tmr_mins * 60LL + tmr_secs) * 1000000LL;
                if (total_us > 0) {
                    tmr_end_time = esp_timer_get_time() + total_us;
                    tmr_running = true;
                }
            }
            dirty = true;
        }
    }

    if (dirty && in_ui) draw_current_ui();
}

void app_alarm_tick(void) {
    time_t now; time(&now);
    struct tm *t = localtime(&now);
    static int last_drawn_sec = -1;
    bool alarm_time_changed = false;
    
    if (t) {
        if (alarm_enabled && t->tm_hour == alarm_hour && t->tm_min == alarm_min && t->tm_sec < 45) {
            if (!alarm_triggered_today) {
                is_ringing = true;
                alarm_triggered_today = true;
            }
        } else if (t->tm_min != alarm_min) {
            alarm_triggered_today = false;
        }
        if (t->tm_sec != last_drawn_sec) {
            last_drawn_sec = t->tm_sec;
            alarm_time_changed = true;
        }
    }

    if (tmr_running) {
        if (esp_timer_get_time() >= tmr_end_time) {
            tmr_running = false;
            tmr_ringing = true;
        }
    }

    bool ringing = is_ringing || tmr_ringing;
    if (ringing) {
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
        static int64_t last_ui_clock_update = 0;
        bool needs_redraw = false;
        
        if (app_mode == MODE_STOPWATCH && sw_running) {
            if (now_ms - last_ui_clock_update >= 50) {
                needs_redraw = true;
            }
        } else if (app_mode == MODE_TIMER && tmr_running) {
            if (now_ms - last_ui_clock_update >= 500) {
                needs_redraw = true;
            }
        } else if (app_mode == MODE_ALARM) {
            if (alarm_time_changed) {
                needs_redraw = true;
            }
        }
        
        if (needs_redraw) {
            last_ui_clock_update = now_ms;
            draw_current_ui();
        }
    }
}
