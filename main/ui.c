#include "ui.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "game_tetris.h"
#include "game_2048.h"
#include "game_pong.h"
#include "game_sound.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI";

static void draw_games_list(void);

#define SCREEN_W 240
#define SCREEN_H 320
#define GAMES_BG 0x0000

static const char *games_list[] = {
    "TETRIS",
    "2048",
    "PONG",
    "RETRO-GO"
};
#define NUM_GAMES 4

static int selected_game = 0;

typedef enum {
    GAME_STATE_MENU = 0,
    GAME_STATE_TETRIS,
    GAME_STATE_2048,
    GAME_STATE_PONG
} game_state_t;

static game_state_t current_game_state = GAME_STATE_MENU;

bool ui_is_in_game(void)
{
    return current_game_state != GAME_STATE_MENU;
}

static void launch_retro_go(void)
{
    ESP_LOGI(TAG, "Launching Retro-Go via OTA...");

    // Show loading screen
    rg_display_drain();
    rg_gui_clear(0x0000);
    rg_gui_set_font_size(16);
    rg_gui_draw_text_box(0, 140, 240, 40, 0x0000, "LOADING RETRO-GO...");
    rg_display_drain();
    rg_gui_set_font_size(8);

    // Find ota_0 partition
    const esp_partition_t *retro_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL
    );

    if (retro_part) {
        esp_err_t err = esp_ota_set_boot_partition(retro_part);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Boot partition set to ota_0, restarting...");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
            rg_gui_clear(0x0000);
            rg_gui_draw_text_box(0, 140, 240, 40, RG_COLOR_RGB(200, 30, 30), "OTA SET FAILED!");
            rg_display_drain();
            vTaskDelay(pdMS_TO_TICKS(2000));
            current_game_state = GAME_STATE_MENU;
            draw_games_list();
        }
    } else {
        ESP_LOGE(TAG, "Retro-Go partition (ota_0) not found!");
        rg_gui_clear(0x0000);
        rg_gui_draw_text_box(0, 140, 240, 40, RG_COLOR_RGB(200, 30, 30), "NO RETRO-GO FW!");
        rg_display_drain();
        vTaskDelay(pdMS_TO_TICKS(2000));
        current_game_state = GAME_STATE_MENU;
        draw_games_list();
    }
}

// ---- Rounded Box Helper ----
static void draw_rounded_box(int x, int y, int w, int h, int r, uint16_t fill_color, uint16_t border_color, int border_width)
{
    static uint16_t *box_buf = NULL;
    static int box_buf_cap = 0;
    int needed = w * h * (int)sizeof(uint16_t);
    if (needed > box_buf_cap) {
        if (box_buf) free(box_buf);
        box_buf = heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!box_buf) box_buf = heap_caps_malloc(needed, MALLOC_CAP_DMA);
        if (!box_buf) box_buf = malloc(needed);
        box_buf_cap = needed;
    }
    if (!box_buf) return;

    uint16_t fill_sw = (uint16_t)((fill_color >> 8) | (fill_color << 8));
    uint16_t border_sw = (uint16_t)((border_color >> 8) | (border_color << 8));

    for (int py = 0; py < h; py++) {
        int cy = 0;
        if (py < r) cy = r - 1 - py;
        else if (py >= h - r) cy = py - (h - r);

        int dx = r;
        if (cy > 0) {
            float fdx = sqrtf((float)(r * r - cy * cy));
            dx = (int)(fdx + 0.5f);
        }
        int lx = r - dx;
        int rx = w - 1 - (r - dx);

        for (int px = 0; px < w; px++) {
            if (px < lx || px > rx) {
                box_buf[py * w + px] = 0x0000;
            } else if (border_width > 0 && (py < border_width || py >= h - border_width || px < lx + border_width || px > rx - border_width)) {
                box_buf[py * w + px] = border_sw;
            } else {
                box_buf[py * w + px] = fill_sw;
            }
        }
    }
    static uint16_t dma_chunk[240 * 20] __attribute__((aligned(4)));
    int lines_per_chunk = 20;
    for (int cy = 0; cy < h; cy += lines_per_chunk) {
        int lines = (cy + lines_per_chunk <= h) ? lines_per_chunk : (h - cy);
        memcpy(dma_chunk, &box_buf[cy * w], lines * w * sizeof(uint16_t));
        rg_display_write(x, y + cy, w, lines, w * 2, dma_chunk);
        rg_display_drain();
    }
}

static void draw_games_list(void)
{
    rg_gui_clear(GAMES_BG);
    rg_display_drain();

    // Top Header
    rg_gui_draw_rect(0, 0, SCREEN_W, 28, GAMES_BG);
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_center(SCREEN_W / 2, 6, "GAMES");
    rg_gui_draw_rect(0, 28, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

    int view_start = selected_game - 2;
    for (int slot = 0; slot < 6; slot++) {
        int idx = view_start + slot;
        int y = 31 + slot * 33;
        bool is_sel = (slot == 2);

        if (idx < 0 || idx >= NUM_GAMES) {
            if (is_sel) {
                draw_rounded_box(6, y + 1, 228, 31, 6, RG_COLOR_RGB(240, 90, 20), RG_COLOR_RGB(240, 90, 20), 0);
                rg_gui_set_font_size(8);
                rg_gui_draw_text_line(12, y + 12, 216, 16, RG_COLOR_RGB(240, 90, 20), RG_COLOR_WHITE, "  ---", 8);
            } else {
                rg_gui_draw_rect(6, y, 228, 33, GAMES_BG);
                if (slot < 5 && slot != 1 && slot != 2) {
                    rg_gui_draw_rect(14, y + 32, 212, 1, RG_COLOR_RGB(40, 40, 45));
                }
            }
            continue;
        }

        uint16_t bg = is_sel ? RG_COLOR_RGB(240, 90, 20) : GAMES_BG;
        uint16_t fg = RG_COLOR_WHITE;

        if (is_sel) {
            draw_rounded_box(6, y + 1, 228, 31, 6, bg, bg, 0);
        } else {
            rg_gui_draw_rect(6, y, 228, 33, GAMES_BG);
        }

        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(12, y + 12, 216, 16, bg, fg, games_list[idx], 8);

        if (slot < 5 && slot != 1 && slot != 2) {
            rg_gui_draw_rect(14, y + 32, 212, 1, RG_COLOR_RGB(40, 40, 45));
        }
    }

}

void ui_init(void)
{
    ESP_LOGI(TAG, "Games page opened");
    selected_game = 0;
    current_game_state = GAME_STATE_MENU;
    draw_games_list();
}

void ui_update(void)
{
    if (current_game_state == GAME_STATE_TETRIS) {
        game_tetris_tick();
    } else if (current_game_state == GAME_STATE_2048) {
        game_2048_tick();
    } else if (current_game_state == GAME_STATE_PONG) {
        game_pong_tick();
    }
}

void ui_handle_input(button_event_t event)
{
    if (current_game_state == GAME_STATE_TETRIS) {
        if (game_tetris_input(event)) {
            rg_display_drain();
            current_game_state = GAME_STATE_MENU;
            draw_games_list();
        }
        return;
    } else if (current_game_state == GAME_STATE_2048) {
        if (game_2048_input(event)) {
            rg_display_drain();
            current_game_state = GAME_STATE_MENU;
            draw_games_list();
        }
        return;
    } else if (current_game_state == GAME_STATE_PONG) {
        if (game_pong_input(event)) {
            rg_display_drain();
            current_game_state = GAME_STATE_MENU;
            draw_games_list();
        }
        return;
    }

    if (event == BTN_UP || event == BTN_VOL_DOWN) {
        selected_game = (selected_game - 1 + NUM_GAMES) % NUM_GAMES;
        draw_games_list();
    } else if (event == BTN_DOWN || event == BTN_VOL_UP) {
        selected_game = (selected_game + 1) % NUM_GAMES;
        draw_games_list();
    } else if (event == BTN_ENTER) {
        if (selected_game == 0) { // TETRIS
            rg_display_drain();
            current_game_state = GAME_STATE_TETRIS;
            game_tetris_start();
        } else if (selected_game == 1) { // 2048
            rg_display_drain();
            current_game_state = GAME_STATE_2048;
            game_2048_start();
        } else if (selected_game == 2) { // PONG
            rg_display_drain();
            current_game_state = GAME_STATE_PONG;
            game_pong_start();
        } else if (selected_game == 3) { // RETRO-GO
            rg_display_drain();
            launch_retro_go();
        }
    }
}
