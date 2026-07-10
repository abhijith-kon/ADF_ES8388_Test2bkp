#include "game_pong.h"
#include "game_sound.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "GAME_PONG";

#define CANV_X 2
#define CANV_Y 32
#define CANV_W 236
#define CANV_H 284

#define PADDLE_W 44
#define PADDLE_H 8
#define BALL_SIZE 6

#define BRICK_ROWS 5
#define BRICK_COLS 8
#define BRICK_W (CANV_W / BRICK_COLS) // 29
#define BRICK_H 14

#define BG_COLOR        RG_COLOR_RGB(10, 10, 16)
#define PADDLE_COLOR    RG_COLOR_RGB(0, 229, 255)
#define BALL_COLOR      RG_COLOR_WHITE
#define HEADER_BG       RG_COLOR_RGB(20, 24, 38)

static const uint16_t row_colors[BRICK_ROWS] = {
    RG_COLOR_RGB(255, 23, 68),   // Red
    RG_COLOR_RGB(255, 109, 0),   // Orange
    RG_COLOR_RGB(255, 214, 0),   // Yellow
    RG_COLOR_RGB(0, 230, 118),   // Green
    RG_COLOR_RGB(0, 229, 255)    // Cyan
};

static int paddle_x;
static int ball_x, ball_y;
static int ball_dx, ball_dy;
static uint8_t bricks[BRICK_ROWS][BRICK_COLS];

static int score = 0;
static int lives = 3;
static bool game_over = false;
static bool ball_launched = false;
static int64_t last_frame_time = 0;

static void draw_header_stats(void) {
    char buf[32];
    rg_gui_set_font_size(8);
    snprintf(buf, sizeof(buf), "SCORE: %d | LIVES: %d", score, lives);
    rg_gui_draw_text_box(0, 0, 240, 28, HEADER_BG, buf);
    rg_gui_draw_rect(0, 28, 240, 1, RG_COLOR_RGB(80, 80, 80));
    rg_gui_draw_rect(0, 318, 240, 2, RG_COLOR_RGB(80, 80, 80));
}

static void render_game(void) {
    static uint16_t dma_chunk[236 * 20] __attribute__((aligned(4)));
    int lines_per_chunk = 20;
    
    int p_top = CANV_H - 12 - PADDLE_H;
    int p_bot = p_top + PADDLE_H;
    int b_right = ball_x + BALL_SIZE;
    int b_bot = ball_y + BALL_SIZE;
    
    for (int cy = 0; cy < CANV_H; cy += lines_per_chunk) {
        int lines = (cy + lines_per_chunk <= CANV_H) ? lines_per_chunk : (CANV_H - cy);
        for (int ly = 0; ly < lines; ly++) {
            int py = cy + ly;
            for (int px = 0; px < CANV_W; px++) {
                uint16_t color = BG_COLOR;
                
                // Check ball
                if (px >= ball_x && px < b_right && py >= ball_y && py < b_bot) {
                    color = BALL_COLOR;
                }
                // Check paddle
                else if (py >= p_top && py < p_bot && px >= paddle_x && px < paddle_x + PADDLE_W) {
                    if (py == p_top || py == p_bot - 1 || px == paddle_x || px == paddle_x + PADDLE_W - 1) {
                        color = RG_COLOR_WHITE;
                    } else {
                        color = PADDLE_COLOR;
                    }
                }
                // Check bricks
                else if (py < BRICK_ROWS * BRICK_H) {
                    int r = py / BRICK_H;
                    int c = px / BRICK_W;
                    if (r >= 0 && r < BRICK_ROWS && c >= 0 && c < BRICK_COLS && bricks[r][c]) {
                        int sub_x = px % BRICK_W;
                        int sub_y = py % BRICK_H;
                        if (sub_x == 0 || sub_x == BRICK_W - 1 || sub_y == 0 || sub_y == BRICK_H - 1) {
                            color = BG_COLOR; // 1px gap between bricks
                        } else {
                            color = row_colors[r];
                        }
                    }
                }
                dma_chunk[ly * CANV_W + px] = (uint16_t)((color >> 8) | (color << 8));
            }
        }
        rg_display_write(CANV_X, CANV_Y + cy, CANV_W, lines, CANV_W * 2, dma_chunk);
        rg_display_drain();
    }
}

static void init_level(void) {
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) bricks[r][c] = 1;
    }
    paddle_x = (CANV_W / 2) - (PADDLE_W / 2);
    ball_x = CANV_W / 2 - (BALL_SIZE / 2);
    ball_y = CANV_H - 12 - PADDLE_H - BALL_SIZE - 2;
    ball_dx = (rand() % 2 == 0) ? 4 : -4;
    ball_dy = -5;
    ball_launched = false;
}

static bool check_win(void) {
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (bricks[r][c] == 1) return false;
        }
    }
    return true;
}

void game_pong_start(void) {
    ESP_LOGI(TAG, "Starting Pong Breakout");
    score = 0;
    lives = 3;
    game_over = false;
    
    rg_gui_clear(BG_COLOR);
    init_level();
    draw_header_stats();
    render_game();
    last_frame_time = esp_timer_get_time();
}

void game_pong_tick(void) {
    if (game_over) return;
    int64_t now = esp_timer_get_time();
    if (now - last_frame_time < 16666) return; // ~60 FPS
    last_frame_time = now;
    
    if (!ball_launched) {
        ball_x = paddle_x + (PADDLE_W / 2) - (BALL_SIZE / 2);
        render_game();
        return;
    }
    
    ball_x += ball_dx;
    ball_y += ball_dy;
    
    // Wall bounce
    if (ball_x <= 0) { ball_x = 0; ball_dx = -ball_dx; }
    if (ball_x + BALL_SIZE >= CANV_W) { ball_x = CANV_W - BALL_SIZE; ball_dx = -ball_dx; }
    if (ball_y <= 0) { ball_y = 0; ball_dy = -ball_dy; }
    
    // Floor fall
    if (ball_y + BALL_SIZE >= CANV_H) {
        lives--;
        draw_header_stats();
        if (lives <= 0) {
            game_over = true;
            rg_gui_set_font_size(16);
            rg_gui_draw_text_box(30, 150, 180, 50, RG_COLOR_RGB(200, 30, 30), "GAME OVER!");
            rg_gui_set_font_size(8);
            game_sound_play_game_over();
            return;
        } else {
            paddle_x = (CANV_W / 2) - (PADDLE_W / 2);
            ball_x = paddle_x + (PADDLE_W / 2) - (BALL_SIZE / 2);
            ball_y = CANV_H - 12 - PADDLE_H - BALL_SIZE - 2;
            ball_launched = false;
            render_game();
            return;
        }
    }
    
    // Paddle bounce
    int p_top = CANV_H - 12 - PADDLE_H;
    if (ball_y + BALL_SIZE >= p_top && ball_y <= p_top + PADDLE_H) {
        if (ball_x + BALL_SIZE >= paddle_x && ball_x <= paddle_x + PADDLE_W && ball_dy > 0) {
            ball_y = p_top - BALL_SIZE;
            ball_dy = -abs(ball_dy);
            int hit_pos = (ball_x + (BALL_SIZE / 2)) - (paddle_x + (PADDLE_W / 2));
            ball_dx = hit_pos / 4;
            if (ball_dx == 0) ball_dx = (rand() % 2 == 0) ? 3 : -3;
        }
    }
    
    // Brick collision
    int center_x = ball_x + (BALL_SIZE / 2);
    int center_y = ball_y + (BALL_SIZE / 2);
    int c = center_x / BRICK_W;
    int r = center_y / BRICK_H;
    
    if (r >= 0 && r < BRICK_ROWS && c >= 0 && c < BRICK_COLS) {
        if (bricks[r][c] == 1) {
            bricks[r][c] = 0;
            ball_dy = -ball_dy;
            score += (BRICK_ROWS - r) * 10;
            draw_header_stats();
            
            if (check_win()) {
                score += 1000;
                draw_header_stats();
                game_sound_play_level_up();
                init_level();
                ball_launched = false;
            }
        }
    }
    
    render_game();
}

bool game_pong_input(button_event_t event) {
    if (event == BTN_ESCAPE || event == BTN_B) {
        return true; // Exit to menu
    }
    if (game_over) {
        if (event == BTN_ENTER) game_pong_start();
        return false;
    }
    if (!ball_launched && (event == BTN_ENTER || event == BTN_A || event == BTN_UP)) {
        ball_launched = true;
    }
    if (event == BTN_LEFT) {
        paddle_x -= 14;
        if (paddle_x < 0) paddle_x = 0;
        if (!ball_launched) ball_x = paddle_x + (PADDLE_W / 2) - (BALL_SIZE / 2);
    } else if (event == BTN_RIGHT) {
        paddle_x += 14;
        if (paddle_x > CANV_W - PADDLE_W) paddle_x = CANV_W - PADDLE_W;
        if (!ball_launched) ball_x = paddle_x + (PADDLE_W / 2) - (BALL_SIZE / 2);
    } else if (event == BTN_VOL_DOWN) { // CCW
        paddle_x -= 12;
        if (paddle_x < 0) paddle_x = 0;
        if (!ball_launched) ball_x = paddle_x + (PADDLE_W / 2) - (BALL_SIZE / 2);
    } else if (event == BTN_VOL_UP) { // CW
        paddle_x += 12;
        if (paddle_x > CANV_W - PADDLE_W) paddle_x = CANV_W - PADDLE_W;
        if (!ball_launched) ball_x = paddle_x + (PADDLE_W / 2) - (BALL_SIZE / 2);
    }
    return false;
}
