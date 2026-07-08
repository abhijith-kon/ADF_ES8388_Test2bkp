#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "app_wifi.h"

static const char *TAG = "APP_WIFI";

#define SCREEN_W 240
#define SCREEN_H 320
#define APP_BG RG_COLOR_BLACK

static int selected_option = 0;
static const char *options[] = {
    "WAP",
    "OTG",
    "OTA"
};
#define NUM_OPTIONS 3

static void draw_rounded_box(int x, int y, int w, int h, int r, uint16_t fill_color, uint16_t border_color, int border_width)
{
    // Minimal block implementation for highlighting
    rg_gui_draw_rect(x, y, w, h, fill_color);
}

static void draw_ui(void)
{
    rg_gui_clear(APP_BG);
    
    // Top Header
    rg_gui_draw_rect(0, 0, SCREEN_W, 28, APP_BG);
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_center(SCREEN_W / 2, 6, "FILE TRANSFER");
    rg_gui_draw_rect(0, 28, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

    // Options List
    for (int i = 0; i < NUM_OPTIONS; i++) {
        int y = 50 + i * 40;
        bool is_sel = (i == selected_option);
        
        uint16_t bg = is_sel ? RG_COLOR_RGB(100, 100, 250) : APP_BG;
        uint16_t fg = RG_COLOR_WHITE;

        if (is_sel) {
            draw_rounded_box(10, y, 220, 32, 6, bg, bg, 0);
        } else {
            rg_gui_draw_rect(10, y, 220, 32, APP_BG);
        }

        rg_gui_set_font_size(16);
        rg_gui_draw_text_center(SCREEN_W / 2, y + 8, options[i]);
    }
    
    rg_display_drain();
}

void app_wifi_init(void)
{
    ESP_LOGI(TAG, "Initialized File Transfer app");
}

void app_wifi_start(void)
{
    ESP_LOGI(TAG, "Started File Transfer app");
    selected_option = 0;
    draw_ui();
}

void app_wifi_stop(void)
{
    ESP_LOGI(TAG, "Stopped File Transfer app");
}

void app_wifi_handle_input(button_event_t event)
{
    if (event == BTN_UP || event == BTN_VOL_DOWN) {
        selected_option--;
        if (selected_option < 0) selected_option = NUM_OPTIONS - 1;
        draw_ui();
    } else if (event == BTN_DOWN || event == BTN_VOL_UP) {
        selected_option++;
        if (selected_option >= NUM_OPTIONS) selected_option = 0;
        draw_ui();
    } else if (event == BTN_ENTER || event == BTN_A) {
        // Future feature entry point
        ESP_LOGI(TAG, "Selected %s", options[selected_option]);
    }
}
