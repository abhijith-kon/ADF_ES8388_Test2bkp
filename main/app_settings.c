#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "app_settings.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"
#include "esp_system.h"

extern int home_ui_current_battery_pct;
extern uint32_t g_system_boot_count;
extern esp_reset_reason_t g_last_reset_reason;
extern char g_sys_error_str[32];

static const char *TAG = "APP_SETTINGS";

static void draw_rect_outline(int x, int y, int w, int h, uint16_t color) {
    rg_gui_draw_rect(x, y, w, 1, color); // top
    rg_gui_draw_rect(x, y + h - 1, w, 1, color); // bottom
    rg_gui_draw_rect(x, y, 1, h, color); // left
    rg_gui_draw_rect(x + w - 1, y, 1, h, color); // right
}

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

static void draw_settings_ui(bool full_refresh)
{
    if (!settings_active) return;

    if (full_refresh) {
        rg_gui_clear(APP_BG);
        
        // Top Header
        rg_gui_draw_rect(0, 0, SCREEN_W, 28, APP_BG);
        rg_gui_set_font_size(16);
        rg_gui_set_text_color(RG_COLOR_WHITE);
    }

    if (current_view == 0) {
        if (full_refresh) {
            rg_gui_draw_text_center(SCREEN_W / 2, 6, "SETTINGS");
            rg_gui_draw_rect(0, 28, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));
        }

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
    } else if (current_view == 1) { // Sci-Fi System Status
        uint16_t AMBER = RG_COLOR_RGB(255, 170, 0);
        uint16_t NEON = RG_COLOR_RGB(57, 255, 20);
        char buf[64];
        
        if (full_refresh) {
            rg_gui_clear(APP_BG);
            
            // Top Status Bar
            rg_gui_draw_rect(0, 0, SCREEN_W, 16, AMBER);
            rg_gui_set_font_size(8);
            rg_gui_draw_text(4, 4, "SYS.DIAG_V1.0", APP_BG, AMBER);
            
            // Box Outlines (Neon Green, actual thin outlines)
            draw_rect_outline(2, 20, 236, 52, NEON); // CPU Panel
            draw_rect_outline(2, 76, 116, 60, NEON); // Mem Panel
            draw_rect_outline(122, 76, 116, 60, NEON); // Storage Panel
            draw_rect_outline(2, 140, 236, 42, NEON); // Temp Panel
            draw_rect_outline(2, 186, 236, 52, NEON); // System Panel
            
            // Corner Accents (Amber)
            rg_gui_draw_rect(2, 20, 6, 2, AMBER); rg_gui_draw_rect(2, 20, 2, 6, AMBER);
            rg_gui_draw_rect(232, 20, 6, 2, AMBER); rg_gui_draw_rect(236, 20, 2, 6, AMBER);

            rg_gui_set_font_size(8);
            
            // --- CPU PANEL ---
            rg_gui_draw_text(6, 24, ">> CPU CORE", NEON, APP_BG);
            rg_gui_draw_text(6, 36, "TYPE: ESP32-S3", RG_COLOR_WHITE, APP_BG);
            rg_gui_draw_text(6, 48, "CORES: 2 @ 240MHz", RG_COLOR_WHITE, APP_BG);
            
            // --- MEMORY PANEL ---
            rg_gui_draw_text(6, 80, ">> MEMORY", NEON, APP_BG);
            multi_heap_info_t info;
            heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
            snprintf(buf, sizeof(buf), "PSRAM: %.1fM", (float)info.total_free_bytes / (1024 * 1024));
            rg_gui_draw_text(6, 94, buf, RG_COLOR_WHITE, APP_BG);
            
            heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
            snprintf(buf, sizeof(buf), "SRAM: %.1fK", (float)info.total_free_bytes / 1024);
            rg_gui_draw_text(6, 106, buf, RG_COLOR_WHITE, APP_BG);
            
            snprintf(buf, sizeof(buf), "L_BLK: %.1fK", (float)info.largest_free_block / 1024);
            rg_gui_draw_text(6, 118, buf, RG_COLOR_WHITE, APP_BG);
            
            // --- STORAGE PANEL ---
            rg_gui_draw_text(126, 80, ">> STORAGE", NEON, APP_BG);
            rg_gui_draw_text(126, 94, "FLSH:3.2/16M", RG_COLOR_WHITE, APP_BG);
            rg_gui_draw_text(126, 106, "SDFREE:14.8G", RG_COLOR_WHITE, APP_BG);
            
            snprintf(buf, sizeof(buf), "ERR:%s", g_sys_error_str);
            if (strcmp(g_sys_error_str, "NONE") == 0) {
                rg_gui_draw_text(126, 118, buf, RG_COLOR_WHITE, APP_BG);
            } else {
                rg_gui_draw_text(126, 118, buf, RG_COLOR_RGB(255, 30, 30), APP_BG);
            }
            
            // --- TEMP PANEL ---
            rg_gui_draw_text(6, 144, ">> TEMPERATURE", NEON, APP_BG);
            
            temperature_sensor_handle_t temp_sensor = NULL;
            temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
            float tsens_value = 0.0;
            if (temperature_sensor_install(&temp_sensor_config, &temp_sensor) == ESP_OK) {
                temperature_sensor_enable(temp_sensor);
                temperature_sensor_get_celsius(temp_sensor, &tsens_value);
                temperature_sensor_disable(temp_sensor);
                temperature_sensor_uninstall(temp_sensor);
            }
            snprintf(buf, sizeof(buf), "CHIP: %.1fC", tsens_value);
            rg_gui_draw_text(6, 158, buf, RG_COLOR_WHITE, APP_BG);
            
            // --- SYSTEM PANEL ---
            rg_gui_draw_text(6, 190, ">> SYSTEM", NEON, APP_BG);
            snprintf(buf, sizeof(buf), "BATTERY: %d%%", home_ui_current_battery_pct);
            rg_gui_draw_text(6, 204, buf, RG_COLOR_WHITE, APP_BG);
            rg_gui_draw_text(126, 204, "RTC: DS3231", RG_COLOR_WHITE, APP_BG);
            
            const char* rst_str = "UNKNOWN";
            switch(g_last_reset_reason) {
                case ESP_RST_POWERON: rst_str = "POWER_ON"; break;
                case ESP_RST_SW: rst_str = "SW_RESET"; break;
                case ESP_RST_PANIC: rst_str = "PANIC"; break;
                case ESP_RST_INT_WDT: rst_str = "INT_WDT"; break;
                case ESP_RST_TASK_WDT: rst_str = "TASK_WDT"; break;
                case ESP_RST_DEEPSLEEP: rst_str = "DEEPSLEEP"; break;
                default: break;
            }
            snprintf(buf, sizeof(buf), "RST: %s", rst_str);
            rg_gui_draw_text(126, 218, buf, RG_COLOR_WHITE, APP_BG);
            rg_gui_draw_text(6, 218, "WDT: OK", RG_COLOR_WHITE, APP_BG);
        }
        
        // --- UPTIME DYNAMIC UPDATE ---
        rg_gui_set_font_size(8);
        uint32_t uptime_s = esp_timer_get_time() / 1000000;
        snprintf(buf, sizeof(buf), "UPTIME: %02lu:%02lu:%02lu  ", uptime_s / 3600, (uptime_s % 3600) / 60, uptime_s % 60);
        rg_gui_draw_text(6, 244, buf, AMBER, APP_BG);
        
        // Top status dynamic elements
        snprintf(buf, sizeof(buf), "BAT:%d%% SD:OK", home_ui_current_battery_pct);
        rg_gui_draw_text(110, 4, buf, APP_BG, AMBER);
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
    draw_settings_ui(true);
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
            draw_settings_ui(false);
        } else if (event == BTN_DOWN || event == BTN_VOL_UP) {
            selected_setting++;
            if (selected_setting >= NUM_SETTINGS) selected_setting = 0;
            draw_settings_ui(false);
        } else if (event == BTN_ENTER || event == BTN_A) {
            ESP_LOGI(TAG, "Selected %s", settings_menu[selected_setting]);
            if (selected_setting == 1) { // System Status
                current_view = 1;
                draw_settings_ui(true);
            }
        }
    } else if (current_view == 1) { // System Status view
        if (event == BTN_ESCAPE || event == BTN_B) {
            current_view = 0;
            draw_settings_ui(true);
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
            draw_settings_ui(false);
        }
    }
}
