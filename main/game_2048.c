#include "game_2048.h"
#include "game_sound.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "GAME_2048";

#define SCREEN_W 240
#define SCREEN_H 320
#define BG_COLOR        RG_COLOR_RGB(16, 20, 36)
#define GRID_BG         RG_COLOR_RGB(30, 37, 62)
#define SCORE_BG        RG_COLOR_RGB(24, 30, 52)
#define TEXT_WHITE      RG_COLOR_WHITE
#define TEXT_DARK       RG_COLOR_RGB(119, 110, 101)

static uint16_t board[4][4];
static int score = 0;
static int best_score = 0;
static bool game_over = false;

static uint16_t get_tile_color(uint16_t val) {
    switch (val) {
        case 0:    return GRID_BG;
        case 2:    return RG_COLOR_RGB(238, 228, 218);
        case 4:    return RG_COLOR_RGB(237, 224, 200);
        case 8:    return RG_COLOR_RGB(242, 177, 121);
        case 16:   return RG_COLOR_RGB(245, 149, 99);
        case 32:   return RG_COLOR_RGB(246, 124, 95);
        case 64:   return RG_COLOR_RGB(246, 94, 59);
        case 128:  return RG_COLOR_RGB(237, 207, 114);
        case 256:  return RG_COLOR_RGB(237, 204, 97);
        case 512:  return RG_COLOR_RGB(237, 200, 80);
        case 1024: return RG_COLOR_RGB(237, 197, 63);
        case 2048: return RG_COLOR_RGB(237, 194, 46);
        default:   return RG_COLOR_RGB(60, 58, 50);
    }
}

static uint16_t get_text_color(uint16_t val) {
    if (val == 2 || val == 4) return TEXT_DARK;
    return TEXT_WHITE;
}

static void draw_score_header(void) {
    char buf[32];
    rg_gui_set_font_size(8);
    
    snprintf(buf, sizeof(buf), "%d", score);
    rg_gui_draw_text_box(110, 12, 58, 38, SCORE_BG, "SCORE");
    rg_gui_set_text_color(RG_COLOR_RGB(255, 214, 0));
    rg_gui_draw_text_center(139, 32, buf);
    
    snprintf(buf, sizeof(buf), "%d", best_score);
    rg_gui_draw_text_box(174, 12, 58, 38, SCORE_BG, "BEST");
    rg_gui_set_text_color(RG_COLOR_RGB(0, 230, 118));
    rg_gui_draw_text_center(203, 32, buf);
    
    rg_gui_set_text_color(TEXT_WHITE);
}

static void draw_tile(int r, int c) {
    int x = 12 + c * 56;
    int y = 92 + r * 56;
    uint16_t val = board[r][c];
    uint16_t bg = get_tile_color(val);
    uint16_t fg = get_text_color(val);
    
    rg_gui_set_text_color(fg);
    if (val == 0) {
        rg_gui_draw_text_box(x, y, 52, 52, bg, "");
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        if (val >= 1000) rg_gui_set_font_size(8);
        else rg_gui_set_font_size(16);
        rg_gui_draw_text_box(x, y, 52, 52, bg, buf);
    }
    rg_gui_set_text_color(TEXT_WHITE);
}

static void render_grid(void) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            draw_tile(r, c);
        }
    }
    draw_score_header();
}

static void spawn_tile(void) {
    int empty[16][2];
    int count = 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (board[r][c] == 0) {
                empty[count][0] = r;
                empty[count][1] = c;
                count++;
            }
        }
    }
    if (count > 0) {
        int idx = rand() % count;
        int r = empty[idx][0];
        int c = empty[idx][1];
        board[r][c] = (rand() % 10 == 0) ? 4 : 2;
    }
}

static bool check_game_over(void) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (board[r][c] == 0) return false;
            if (c < 3 && board[r][c] == board[r][c + 1]) return false;
            if (r < 3 && board[r][c] == board[r + 1][c]) return false;
        }
    }
    return true;
}

static bool slide_left(void) {
    bool moved = false;
    for (int r = 0; r < 4; r++) {
        int write_pos = 0;
        int last_merge = -1;
        for (int c = 0; c < 4; c++) {
            if (board[r][c] != 0) {
                if (write_pos > 0 && board[r][write_pos - 1] == board[r][c] && last_merge != write_pos - 1) {
                    board[r][write_pos - 1] *= 2;
                    score += board[r][write_pos - 1];
                    if (score > best_score) best_score = score;
                    board[r][c] = 0;
                    last_merge = write_pos - 1;
                    moved = true;
                } else {
                    if (write_pos != c) {
                        board[r][write_pos] = board[r][c];
                        board[r][c] = 0;
                        moved = true;
                    }
                    write_pos++;
                }
            }
        }
    }
    return moved;
}

static bool slide_right(void) {
    bool moved = false;
    for (int r = 0; r < 4; r++) {
        int write_pos = 3;
        int last_merge = -1;
        for (int c = 3; c >= 0; c--) {
            if (board[r][c] != 0) {
                if (write_pos < 3 && board[r][write_pos + 1] == board[r][c] && last_merge != write_pos + 1) {
                    board[r][write_pos + 1] *= 2;
                    score += board[r][write_pos + 1];
                    if (score > best_score) best_score = score;
                    board[r][c] = 0;
                    last_merge = write_pos + 1;
                    moved = true;
                } else {
                    if (write_pos != c) {
                        board[r][write_pos] = board[r][c];
                        board[r][c] = 0;
                        moved = true;
                    }
                    write_pos--;
                }
            }
        }
    }
    return moved;
}

static bool slide_up(void) {
    bool moved = false;
    for (int c = 0; c < 4; c++) {
        int write_pos = 0;
        int last_merge = -1;
        for (int r = 0; r < 4; r++) {
            if (board[r][c] != 0) {
                if (write_pos > 0 && board[write_pos - 1][c] == board[r][c] && last_merge != write_pos - 1) {
                    board[write_pos - 1][c] *= 2;
                    score += board[write_pos - 1][c];
                    if (score > best_score) best_score = score;
                    board[r][c] = 0;
                    last_merge = write_pos - 1;
                    moved = true;
                } else {
                    if (write_pos != r) {
                        board[write_pos][c] = board[r][c];
                        board[r][c] = 0;
                        moved = true;
                    }
                    write_pos++;
                }
            }
        }
    }
    return moved;
}

static bool slide_down(void) {
    bool moved = false;
    for (int c = 0; c < 4; c++) {
        int write_pos = 3;
        int last_merge = -1;
        for (int r = 3; r >= 0; r--) {
            if (board[r][c] != 0) {
                if (write_pos < 3 && board[write_pos + 1][c] == board[r][c] && last_merge != write_pos + 1) {
                    board[write_pos + 1][c] *= 2;
                    score += board[write_pos + 1][c];
                    if (score > best_score) best_score = score;
                    board[r][c] = 0;
                    last_merge = write_pos + 1;
                    moved = true;
                } else {
                    if (write_pos != r) {
                        board[write_pos][c] = board[r][c];
                        board[r][c] = 0;
                        moved = true;
                    }
                    write_pos--;
                }
            }
        }
    }
    return moved;
}

void game_2048_start(void) {
    ESP_LOGI(TAG, "Starting 2048");
    memset(board, 0, sizeof(board));
    score = 0;
    game_over = false;
    
    rg_gui_clear(BG_COLOR);
    
    // Header
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_RGB(246, 124, 95));
    rg_gui_draw_text(16, 20, "2048", RG_COLOR_RGB(246, 124, 95), BG_COLOR);
    rg_gui_set_font_size(8);
    rg_gui_set_text_color(TEXT_WHITE);
    rg_gui_draw_text(16, 45, "Join tiles to 2048!", TEXT_WHITE, BG_COLOR);
    
    // Grid border/background box
    rg_gui_draw_rect(8, 88, 224, 224, GRID_BG);
    
    spawn_tile();
    spawn_tile();
    render_grid();
}

void game_2048_tick(void) {
    // Event driven
}

bool game_2048_input(button_event_t event) {
    if (event == BTN_ESCAPE || event == BTN_B) {
        return true; // Exit to menu
    }
    if (game_over) {
        if (event == BTN_ENTER) game_2048_start();
        return false;
    }
    
    bool moved = false;
    int old_score = score;
    if (event == BTN_LEFT || event == BTN_VOL_DOWN) {
        moved = slide_left();
    } else if (event == BTN_RIGHT || event == BTN_VOL_UP) {
        moved = slide_right();
    } else if (event == BTN_UP) {
        moved = slide_up();
    } else if (event == BTN_DOWN) {
        moved = slide_down();
    }
    
    if (moved) {
        spawn_tile();
        render_grid();
        if (score - old_score >= 64) {
            game_sound_play_milestone_2048();
        }
        if (check_game_over()) {
            game_over = true;
            rg_gui_set_font_size(16);
            rg_gui_draw_text_box(30, 175, 180, 50, RG_COLOR_RGB(200, 30, 30), "GAME OVER!");
            rg_gui_set_font_size(8);
            game_sound_play_gameover_2048();
        }
    }
    return false;
}
