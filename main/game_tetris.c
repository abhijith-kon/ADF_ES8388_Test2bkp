#include "game_tetris.h"
#include "game_sound.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "GAME_TETRIS";

#define BOARD_W 10
#define BOARD_H 20
#define BLOCK_SIZE 14
#define BOARD_X 6
#define BOARD_Y 34

#define BG_COLOR        RG_COLOR_RGB(12, 14, 22)
#define BORDER_COLOR    RG_COLOR_RGB(60, 65, 90)
#define SIDEBAR_BG      RG_COLOR_RGB(20, 24, 38)
#define TEXT_COLOR      RG_COLOR_WHITE
#define HIGHLIGHT_COLOR RG_COLOR_RGB(0, 229, 255)

static uint8_t board[BOARD_H][BOARD_W];
static int score = 0;
static int total_lines = 0;
static int level = 1;
static bool game_over = false;
static int64_t last_drop_time = 0;

static int bag[7];
static int bag_idx = 7;

static int px, py;
static int curr_shape, curr_rot;
static int next_shape;

static const uint16_t shapes[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444}, // I
    {0x44C0, 0x8E00, 0x6440, 0x0E20}, // J
    {0x4460, 0x0E80, 0xC440, 0x2E00}, // L
    {0xCC00, 0xCC00, 0xCC00, 0xCC00}, // O
    {0x06C0, 0x8C40, 0x6C00, 0x4620}, // S
    {0x0E40, 0x4C40, 0x4E00, 0x4640}, // T
    {0x0C60, 0x4C80, 0xC600, 0x2640}  // Z
};

static const uint16_t palette[8] = {
    BG_COLOR,
    RG_COLOR_RGB(0, 229, 255),   // 1: I (Cyan)
    RG_COLOR_RGB(41, 121, 255),  // 2: J (Blue)
    RG_COLOR_RGB(255, 109, 0),   // 3: L (Orange)
    RG_COLOR_RGB(255, 214, 0),   // 4: O (Yellow)
    RG_COLOR_RGB(0, 230, 118),   // 5: S (Green)
    RG_COLOR_RGB(213, 0, 249),   // 6: T (Purple)
    RG_COLOR_RGB(255, 23, 68)    // 7: Z (Red)
};

static void fill_bag(void) {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = bag[i];
        bag[i] = bag[j];
        bag[j] = temp;
    }
    bag_idx = 0;
}

static int pull_from_bag(void) {
    if (bag_idx >= 7) fill_bag();
    return bag[bag_idx++];
}

static bool check_collision(int shape, int rot, int cx, int cy) {
    for (int i = 0; i < 16; i++) {
        if (shapes[shape][rot] & (1 << (15 - i))) {
            int bx = cx + (i % 4);
            int by = cy + (i / 4);
            if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) return true;
            if (by >= 0 && board[by][bx]) return true;
        }
    }
    return false;
}

static void draw_sidebar_stats(void) {
    char buf[32];
    rg_gui_set_font_size(8);
    
    snprintf(buf, sizeof(buf), "%d", score);
    rg_gui_draw_text_box(154, 116, 80, 46, SIDEBAR_BG, "SCORE");
    rg_gui_set_text_color(HIGHLIGHT_COLOR);
    rg_gui_draw_text_center(194, 138, buf);
    
    snprintf(buf, sizeof(buf), "%d", level);
    rg_gui_draw_text_box(154, 172, 80, 46, SIDEBAR_BG, "LEVEL");
    rg_gui_set_text_color(RG_COLOR_RGB(255, 214, 0));
    rg_gui_draw_text_center(194, 194, buf);
    
    snprintf(buf, sizeof(buf), "%d", total_lines);
    rg_gui_draw_text_box(154, 228, 80, 46, SIDEBAR_BG, "LINES");
    rg_gui_set_text_color(RG_COLOR_RGB(0, 230, 118));
    rg_gui_draw_text_center(194, 250, buf);
    rg_gui_set_text_color(TEXT_COLOR);
}

static void render_next_piece(void) {
    rg_gui_draw_text_box(154, 36, 80, 70, SIDEBAR_BG, "NEXT");
    static uint16_t box_buf[50 * 40] __attribute__((aligned(4)));
    int w = 50, h = 40;
    uint16_t bg_sw = (uint16_t)((SIDEBAR_BG >> 8) | (SIDEBAR_BG << 8));
    for (int i = 0; i < w * h; i++) box_buf[i] = bg_sw;
    
    uint16_t col = palette[next_shape + 1];
    uint16_t col_sw = (uint16_t)((col >> 8) | (col << 8));
    uint16_t border_sw = (uint16_t)((BORDER_COLOR >> 8) | (BORDER_COLOR << 8));
    
    int ox = (w - 4 * 10) / 2;
    int oy = (h - 4 * 10) / 2;
    for (int i = 0; i < 16; i++) {
        if (shapes[next_shape][0] & (1 << (15 - i))) {
            int bx = ox + (i % 4) * 10;
            int by = oy + (i / 4) * 10;
            for (int r = 0; r < 10; r++) {
                for (int c = 0; c < 10; c++) {
                    int px = bx + c;
                    int py = by + r;
                    if (px >= 0 && px < w && py >= 0 && py < h) {
                        if (r == 0 || c == 0 || r == 9 || c == 9) box_buf[py * w + px] = border_sw;
                        else box_buf[py * w + px] = col_sw;
                    }
                }
            }
        }
    }
    rg_display_write(169, 56, w, h, w * 2, box_buf);
    rg_display_drain();
}

static void render_board(void) {
    static uint16_t dma_chunk[140 * 20] __attribute__((aligned(4)));
    int lines_per_chunk = 20;
    int w = BOARD_W * BLOCK_SIZE; // 140
    int h = BOARD_H * BLOCK_SIZE; // 280
    
    for (int cy = 0; cy < h; cy += lines_per_chunk) {
        int lines = (cy + lines_per_chunk <= h) ? lines_per_chunk : (h - cy);
        for (int ly = 0; ly < lines; ly++) {
            int py_abs = cy + ly;
            int by = py_abs / BLOCK_SIZE;
            int sub_y = py_abs % BLOCK_SIZE;
            for (int bx = 0; bx < BOARD_W; bx++) {
                uint8_t val = board[by][bx];
                // Check active falling piece
                if (!val && !game_over) {
                    for (int i = 0; i < 16; i++) {
                        if (shapes[curr_shape][curr_rot] & (1 << (15 - i))) {
                            int sx = px + (i % 4);
                            int sy = py + (i / 4);
                            if (sx == bx && sy == by) {
                                val = curr_shape + 1;
                                break;
                            }
                        }
                    }
                }
                uint16_t color = palette[val];
                if (val > 0) {
                    if (sub_y == 0 || sub_y == BLOCK_SIZE - 1) {
                        color = BORDER_COLOR;
                    } else {
                        for (int sub_x = 0; sub_x < BLOCK_SIZE; sub_x++) {
                            uint16_t c = (sub_x == 0 || sub_x == BLOCK_SIZE - 1) ? BORDER_COLOR : color;
                            dma_chunk[ly * w + bx * BLOCK_SIZE + sub_x] = (uint16_t)((c >> 8) | (c << 8));
                        }
                        continue;
                    }
                }
                uint16_t col_sw = (uint16_t)((color >> 8) | (color << 8));
                for (int sub_x = 0; sub_x < BLOCK_SIZE; sub_x++) {
                    dma_chunk[ly * w + bx * BLOCK_SIZE + sub_x] = col_sw;
                }
            }
        }
        rg_display_write(BOARD_X, BOARD_Y + cy, w, lines, w * 2, dma_chunk);
        rg_display_drain();
    }
}

static void lock_piece(void) {
    for (int i = 0; i < 16; i++) {
        if (shapes[curr_shape][curr_rot] & (1 << (15 - i))) {
            int bx = px + (i % 4);
            int by = py + (i / 4);
            if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
                board[by][bx] = curr_shape + 1;
            }
        }
    }
}

static void clear_lines(void) {
    int lines_cleared = 0;
    for (int y = BOARD_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < BOARD_W; x++) {
            if (!board[y][x]) { full = false; break; }
        }
        if (full) {
            lines_cleared++;
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_W; x++) board[yy][x] = board[yy-1][x];
            }
            for (int x = 0; x < BOARD_W; x++) board[0][x] = 0;
            y++;
        }
    }
    if (lines_cleared > 0) {
        score += lines_cleared * 100 * level;
        total_lines += lines_cleared;
        int new_lvl = (total_lines / 10) + 1;
        if (new_lvl > level) {
            level = new_lvl;
            game_sound_play_level_up();
        } else {
            game_sound_play_score_tetris();
        }
        draw_sidebar_stats();
    }
}

static void spawn_piece(void) {
    curr_shape = next_shape;
    curr_rot = 0;
    px = 3; py = -2;
    next_shape = pull_from_bag();
    render_next_piece();
    
    if (check_collision(curr_shape, curr_rot, px, py)) {
        game_over = true;
        rg_gui_draw_text_box(154, 284, 80, 30, RG_COLOR_RGB(200, 20, 20), "FAIL!");
        game_sound_play_gameover_tetris();
    }
}

void game_tetris_start(void) {
    ESP_LOGI(TAG, "Starting Tetris");
    memset(board, 0, sizeof(board));
    score = 0;
    total_lines = 0;
    level = 1;
    game_over = false;
    fill_bag();
    next_shape = pull_from_bag();
    
    rg_gui_clear(BG_COLOR);
    rg_gui_draw_rect(0, 0, 240, 28, RG_COLOR_RGB(20, 24, 38));
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(HIGHLIGHT_COLOR);
    rg_gui_draw_text_center(120, 6, "TETRIS ARCADE");
    rg_gui_draw_rect(0, 28, 240, 1, BORDER_COLOR);
    rg_gui_set_text_color(TEXT_COLOR);
    
    // Draw board border
    rg_gui_draw_rect(BOARD_X - 1, BOARD_Y - 1, BOARD_W * BLOCK_SIZE + 2, BOARD_H * BLOCK_SIZE + 2, BORDER_COLOR);
    
    draw_sidebar_stats();
    spawn_piece();
    render_board();
    last_drop_time = esp_timer_get_time();
}

void game_tetris_tick(void) {
    if (game_over) return;
    int64_t now = esp_timer_get_time();
    int64_t speed_us = 350000 - ((level - 1) * 45000);
    if (speed_us < 80000) speed_us = 80000;
    
    if (now - last_drop_time >= speed_us) {
        last_drop_time = now;
        if (!check_collision(curr_shape, curr_rot, px, py + 1)) {
            py++;
            render_board();
        } else {
            lock_piece();
            clear_lines();
            spawn_piece();
            render_board();
        }
    }
}

bool game_tetris_input(button_event_t event) {
    if (event == BTN_ESCAPE || event == BTN_B) {
        return true; // Exit to games menu
    }
    if (game_over) {
        if (event == BTN_ENTER) game_tetris_start();
        return false;
    }
    bool moved = false;
    if (event == BTN_LEFT || event == BTN_VOL_DOWN) {
        if (!check_collision(curr_shape, curr_rot, px - 1, py)) {
            px--;
            moved = true;
        }
    } else if (event == BTN_RIGHT || event == BTN_VOL_UP) {
        if (!check_collision(curr_shape, curr_rot, px + 1, py)) {
            px++;
            moved = true;
        }
    } else if (event == BTN_UP || event == BTN_A || event == BTN_ENTER) {
        int next_rot = (curr_rot + 1) % 4;
        if (!check_collision(curr_shape, next_rot, px, py)) {
            curr_rot = next_rot;
            moved = true;
        } else if (!check_collision(curr_shape, next_rot, px - 1, py)) {
            px--; curr_rot = next_rot; moved = true;
        } else if (!check_collision(curr_shape, next_rot, px + 1, py)) {
            px++; curr_rot = next_rot; moved = true;
        }
    } else if (event == BTN_DOWN) {
        if (!check_collision(curr_shape, curr_rot, px, py + 1)) {
            py++;
            last_drop_time = esp_timer_get_time();
            moved = true;
        }
    }
    if (moved) {
        render_board();
    }
    return false;
}
