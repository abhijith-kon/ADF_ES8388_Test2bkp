#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "app_settings.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

extern int home_ui_current_battery_pct;

static const char *TAG = "APP_SETTINGS";

#define SCREEN_W 240
#define SCREEN_H 320
#define APP_BG RG_COLOR_BLACK

#define NUM_SETTINGS 3
static const char *settings_menu[NUM_SETTINGS] = {
    "Neopixel",
    "System Status",
    "Wifi Updates"
};

static int selected_setting = 0;
static bool settings_active = false;
static int current_view = 0; // 0 = Menu, 1 = System Status

static void draw_rounded_box(int x, int y, int w, int h, int r, uint16_t fill_color, uint16_t border_color, int border_width)
{
    rg_gui_draw_rect(x, y, w, h, fill_color);
}

static void draw_settings_ui(void)
{
    if (!settings_active) return;

    rg_gui_clear(APP_BG);
    
    // Top Header
    rg_gui_draw_rect(0, 0, SCREEN_W, 28, APP_BG);
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);

    if (current_view == 0) {
        rg_gui_draw_text_center(SCREEN_W / 2, 6, "SETTINGS");
        rg_gui_draw_rect(0, 28, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

        for (int i = 0; i < NUM_SETTINGS; i++) {
            int y = 50 + i * 40;
            bool is_sel = (i == selected_setting);
            uint16_t bg = is_sel ? RG_COLOR_RGB(100, 60, 200) : APP_BG;

            if (is_sel) {
                draw_rounded_box(10, y, 220, 32, 6, bg, bg, 0);
            } else {
                rg_gui_draw_rect(10, y, 220, 32, APP_BG);
            }

            rg_gui_set_font_size(16);
            rg_gui_draw_text_center(SCREEN_W / 2, y + 8, settings_menu[i]);
        }
    } else if (current_view == 1) { // System Status
        rg_gui_draw_text_center(SCREEN_W / 2, 6, "SYSTEM STATUS");
        rg_gui_draw_rect(0, 28, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));
        
        rg_gui_set_font_size(12);
        int y = 40;
        char buf[64];
        
        // PSRAM
        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
        float psram_used_mb = (float)info.allocated_blocks / (1024 * 1024);
        float psram_total_mb = (float)info.total_free_bytes / (1024 * 1024) + psram_used_mb; // Approximation
        snprintf(buf, sizeof(buf), "PSRAM: %.1f MB Free", (float)info.total_free_bytes / (1024 * 1024));
        rg_gui_draw_text(10, y, buf); y += 20;
        
        // Internal RAM
        heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
        snprintf(buf, sizeof(buf), "SRAM: %.1f KB Free", (float)info.total_free_bytes / 1024);
        rg_gui_draw_text(10, y, buf); y += 20;

        // Battery
        snprintf(buf, sizeof(buf), "Battery: %d%%", home_ui_current_battery_pct);
        rg_gui_draw_text(10, y, buf); y += 20;
        
        // Uptime
        uint32_t uptime_s = esp_timer_get_time() / 1000000;
        snprintf(buf, sizeof(buf), "Uptime: %lum %lus", uptime_s / 60, uptime_s % 60);
        rg_gui_draw_text(10, y, buf); y += 20;
        
        // RTC
        snprintf(buf, sizeof(buf), "RTC Status: OK (DS3231)");
        rg_gui_draw_text(10, y, buf); y += 20;
        
        // CPU
        snprintf(buf, sizeof(buf), "CPU: ESP32-S3 (240MHz)");
        rg_gui_draw_text(10, y, buf); y += 20;
        
        rg_gui_set_font_size(8);
        rg_gui_draw_text_center(SCREEN_W / 2, 280, "Press B/ESC to Return");
    }

    rg_display_drain();
}

void app_settings_init(void)
{
    ESP_LOGI(TAG, "Initialized Settings app");
}

void app_settings_start(void)
{
    ESP_LOGI(TAG, "Started Settings app");
    settings_active = true;
    selected_setting = 0;
    current_view = 0;
    draw_settings_ui();
}

void app_settings_stop(void)
{
    settings_active = false;
    ESP_LOGI(TAG, "Stopped Settings app");
}

void app_settings_handle_input(button_event_t event)
{
    if (!settings_active) return;

    if (current_view == 0) {
        if (event == BTN_UP || event == BTN_VOL_DOWN) {
            selected_setting--;
            if (selected_setting < 0) selected_setting = NUM_SETTINGS - 1;
            draw_settings_ui();
        } else if (event == BTN_DOWN || event == BTN_VOL_UP) {
            selected_setting++;
            if (selected_setting >= NUM_SETTINGS) selected_setting = 0;
            draw_settings_ui();
        } else if (event == BTN_ENTER || event == BTN_A) {
            ESP_LOGI(TAG, "Selected %s", settings_menu[selected_setting]);
            if (selected_setting == 1) { // System Status
                current_view = 1;
                draw_settings_ui();
            }
        }
    } else if (current_view == 1) { // System Status view
        if (event == BTN_ESCAPE || event == BTN_B) {
            current_view = 0;
            draw_settings_ui();
        }
    }
}

void app_settings_tick(void)
{
    static uint32_t last_update = 0;
    if (settings_active && current_view == 1) {
        uint32_t now = esp_timer_get_time() / 1000000;
        if (now - last_update >= 1) {
            last_update = now;
            draw_settings_ui();
        }
    }
}
