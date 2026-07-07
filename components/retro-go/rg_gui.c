#include "rg_gui.h"
#include "rg_display.h"
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "rg_gui";

static void *rg_gui_dma_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!ptr) ptr = heap_caps_malloc(size, MALLOC_CAP_DMA);
    if (!ptr) ptr = malloc(size);
    return ptr;
}

static void rg_gui_send_dma_chunked(int x, int y, int w, int h, const uint16_t *buf)
{
    static uint16_t dma_chunk[240 * 20] __attribute__((aligned(4)));
    int lines_per_chunk = 20;
    for (int cy = 0; cy < h; cy += lines_per_chunk) {
        int lines = (cy + lines_per_chunk <= h) ? lines_per_chunk : (h - cy);
        ESP_LOGI(TAG, "chunk y=%d lines=%d", cy, lines);
        memcpy(dma_chunk, &buf[cy * w], lines * w * sizeof(uint16_t));
        rg_display_write(x, y + cy, w, lines, w * 2, dma_chunk);
        rg_display_drain();
    }
}

// Global state for new primitives
static uint16_t current_fill_color = 0xFFFF;
static uint16_t current_stroke_color = 0xFFFF;
static int current_stroke_width = 1;
static uint16_t current_text_color = 0xFFFF;
static int current_font_scale = 1;

// Simple 8x8 font
static const uint8_t font8x8_basic[128][8] = {
    [0x20] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // space
    [0x30] = { 0x3E, 0x61, 0x61, 0x61, 0x61, 0x61, 0x3E, 0x00 }, // 0
    [0x31] = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, // 1
    [0x32] = { 0x3E, 0x61, 0x01, 0x01, 0x3E, 0x40, 0x7F, 0x00 }, // 2
    [0x33] = { 0x3E, 0x61, 0x01, 0x1E, 0x01, 0x61, 0x3E, 0x00 }, // 3
    [0x34] = { 0x06, 0x0E, 0x16, 0x26, 0x7F, 0x06, 0x06, 0x00 }, // 4
    [0x35] = { 0x7F, 0x40, 0x7E, 0x01, 0x01, 0x61, 0x3E, 0x00 }, // 5
    [0x36] = { 0x3E, 0x60, 0x7E, 0x61, 0x61, 0x61, 0x3E, 0x00 }, // 6
    [0x37] = { 0x7F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00 }, // 7
    [0x38] = { 0x3E, 0x61, 0x61, 0x3E, 0x61, 0x61, 0x3E, 0x00 }, // 8
    [0x39] = { 0x3E, 0x61, 0x61, 0x3F, 0x01, 0x01, 0x3E, 0x00 }, // 9
    [0x3A] = { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00 }, // :
    [0x41] = { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00 }, // A
    [0x42] = { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 }, // B
    [0x43] = { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 }, // C
    [0x44] = { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 }, // D
    [0x45] = { 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00 }, // E
    [0x46] = { 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00 }, // F
    [0x47] = { 0x3E, 0x61, 0x60, 0x6F, 0x61, 0x61, 0x3E, 0x00 }, // G
    [0x48] = { 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 }, // H
    [0x49] = { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, // I
    [0x4A] = { 0x0F, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00 }, // J
    [0x4B] = { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 }, // K
    [0x4C] = { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 }, // L
    [0x4D] = { 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00 }, // M
    [0x4E] = { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00 }, // N
    [0x4F] = { 0x3E, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3E, 0x00 }, // O
    [0x50] = { 0x7E, 0x63, 0x63, 0x7E, 0x60, 0x60, 0x60, 0x00 }, // P
    [0x51] = { 0x3E, 0x63, 0x63, 0x63, 0x6B, 0x67, 0x3E, 0x0E }, // Q
    [0x52] = { 0x7E, 0x63, 0x63, 0x7E, 0x78, 0x6C, 0x66, 0x00 }, // R
    [0x53] = { 0x3E, 0x63, 0x38, 0x0E, 0x07, 0x63, 0x3E, 0x00 }, // S
    [0x54] = { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 }, // T
    [0x55] = { 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3E, 0x00 }, // U
    [0x56] = { 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00 }, // V
    [0x57] = { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 }, // W
    [0x58] = { 0x63, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x63, 0x00 }, // X
    [0x59] = { 0x63, 0x63, 0x36, 0x1C, 0x18, 0x18, 0x18, 0x00 }, // Y
    [0x5A] = { 0x7F, 0x03, 0x06, 0x1C, 0x30, 0x60, 0x7F, 0x00 }, // Z
    [0x21] = { 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x00 }, // !
    [0x22] = { 0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 }, // "
    [0x23] = { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00 }, // #
    [0x24] = { 0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00 }, // $
    [0x25] = { 0x62, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x46, 0x00 }, // %
    [0x26] = { 0x38, 0x6C, 0x38, 0x76, 0x66, 0x66, 0x3B, 0x00 }, // &
    [0x27] = { 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 }, // '
    [0x28] = { 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00 }, // (
    [0x29] = { 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00 }, // )
    [0x2A] = { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 }, // *
    [0x2B] = { 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00 }, // +
    [0x2C] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 }, // ,
    [0x2D] = { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 }, // -
    [0x2E] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 }, // .
    [0x2F] = { 0x02, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00 }, // /
    [0x3B] = { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00 }, // ;
    [0x3C] = { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 }, // <
    [0x3D] = { 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00 }, // =
    [0x3E] = { 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00 }, // >
    [0x3F] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00 }, // ?
    [0x40] = { 0x3C, 0x42, 0x5E, 0x52, 0x5E, 0x40, 0x3C, 0x00 }, // @
    [0x5B] = { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 }, // [
    [0x5C] = { 0x40, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00 }, // backslash
    [0x5D] = { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 }, // ]
    [0x5E] = { 0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00 }, // ^
    [0x5F] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F }, // _
    [0x60] = { 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 }, // `
    [0x61] = { 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00 }, // a
    [0x62] = { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00 }, // b
    [0x63] = { 0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00 }, // c
    [0x64] = { 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00 }, // d
    [0x65] = { 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00 }, // e
    [0x66] = { 0x1C, 0x36, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00 }, // f
    [0x67] = { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C }, // g
    [0x68] = { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 }, // h
    [0x69] = { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 }, // i
    [0x6A] = { 0x06, 0x00, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C }, // j
    [0x6B] = { 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00 }, // k
    [0x6C] = { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, // l
    [0x6D] = { 0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00 }, // m
    [0x6E] = { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 }, // n
    [0x6F] = { 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00 }, // o
    [0x70] = { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60 }, // p
    [0x71] = { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06 }, // q
    [0x72] = { 0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00 }, // r
    [0x73] = { 0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00 }, // s
    [0x74] = { 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18, 0x0E, 0x00 }, // t
    [0x75] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00 }, // u
    [0x76] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 }, // v
    [0x77] = { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00 }, // w
    [0x78] = { 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00 }, // x
    [0x79] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C }, // y
    [0x7A] = { 0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00 }, // z
    [0x7B] = { 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00 }, // {
    [0x7C] = { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 }, // |
    [0x7D] = { 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00 }, // }
    [0x7E] = { 0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00, 0x00 }, // ~
};

void rg_gui_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg_color)
{
    int len = strlen(text);
    // Persistent static buffer — avoids malloc/free per call (DMA-safe)
    static uint16_t char_buf[64] __attribute__((aligned(4))); // 8x8 = 64 pixels

    // Byte-swap for ILI9341 big-endian RGB565 (ESP32 is little-endian)
    uint16_t color_sw = ((color >> 8) | (color << 8));
    uint16_t bg_sw = ((bg_color >> 8) | (bg_color << 8));

    for (int i = 0; i < len; i++) {
        uint8_t c = text[i];
        if (c > 127) c = '?';
        
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[c][row];
            for (int col = 0; col < 8; col++) {
                char_buf[row * 8 + col] = (bits & (1 << (7 - col))) ? color_sw : bg_sw;
            }
        }
        rg_display_write(x + i * 8, y, 8, 8, 8 * 2, char_buf);
        rg_display_drain();
    }
}

void rg_gui_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    // Pre-allocated static buffer — never freed, prevents DMA corruption on realloc.
    // Max 2048 uint16_t = 4096 bytes, sufficient for any width (max_lines = 4096/(w*2)).
    static uint16_t rect_buf[2048] __attribute__((aligned(4)));

    int max_pixels = (int)(sizeof(rect_buf) / sizeof(rect_buf[0]));
    if (w > max_pixels) return;
    int max_lines = max_pixels / w;
    if (max_lines == 0) max_lines = 1;
    if (max_lines > h) max_lines = h;

    ESP_LOGI(TAG, "rect w=%d h=%d max_lines=%d", w, h, max_lines);

    // Byte-swap for ILI9341 big-endian RGB565 (ESP32 is little-endian)
    uint16_t color_sw = ((color >> 8) | (color << 8));
    int fill_count = w * max_lines;
    for (int i = 0; i < fill_count; i++) rect_buf[i] = color_sw;

    int lines_sent = 0;
    while (lines_sent < h) {
        int lines = h - lines_sent;
        if (lines > max_lines) lines = max_lines;
        rg_display_write(x, y + lines_sent, w, lines, w * 2, rect_buf);
        rg_display_drain();
        lines_sent += lines;
    }
}

void rg_gui_clear(uint16_t color)
{
    rg_gui_draw_rect(0, 0, 240, 320, color);
}

void rg_gui_set_font_size(int size_px)
{
    // Our base font is 8x8.
    current_font_scale = size_px / 8;
    if (current_font_scale < 1) current_font_scale = 1;
}

void rg_gui_set_text_color(uint16_t color)
{
    current_text_color = color;
}

void rg_gui_set_fill_color(uint16_t color)
{
    current_fill_color = color;
}

void rg_gui_set_stroke_color(uint16_t color)
{
    current_stroke_color = color;
}

void rg_gui_set_stroke_width(int width)
{
    current_stroke_width = width;
}

void rg_gui_draw_text_center(int x, int y, const char *text)
{
    int len = strlen(text);
    int char_w = 8 * current_font_scale;
    int total_w = len * char_w;
    int start_x = x - (total_w / 2);
    
    static uint16_t *str_bufs[4] = {NULL, NULL, NULL, NULL};
    static int str_buf_sizes[4] = {0, 0, 0, 0};
    static int buf_idx = 0;
    
    buf_idx = (buf_idx + 1) % 4;
    
    int needed_size = total_w * char_w * 2;
    if (needed_size > str_buf_sizes[buf_idx]) {
        if (str_bufs[buf_idx]) free(str_bufs[buf_idx]);
        str_bufs[buf_idx] = rg_gui_dma_malloc(needed_size);
        str_buf_sizes[buf_idx] = needed_size;
    }
    if (!str_bufs[buf_idx]) return;
    uint16_t *str_buf = str_bufs[buf_idx];

    uint16_t fg_swapped = ((current_text_color >> 8) | (current_text_color << 8));
    
    for (int i = 0; i < len; i++) {
        uint8_t c = text[i];
        if (c > 127) c = '?';
        
        // Render char into the overall string buffer
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[c][row];
            for (int col = 0; col < 8; col++) {
                uint16_t color = (bits & (1 << (7 - col))) ? fg_swapped : 0x0000;
                
                // Scale it up
                for (int sy = 0; sy < current_font_scale; sy++) {
                    for (int sx = 0; sx < current_font_scale; sx++) {
                        int pixel_x = (i * char_w) + (col * current_font_scale + sx);
                        int pixel_y = (row * current_font_scale + sy);
                        str_buf[pixel_y * total_w + pixel_x] = color;
                    }
                }
            }
        }
    }
    
    rg_gui_send_dma_chunked(start_x, y, total_w, char_w, str_buf);
    // Drain so str_buf slot is safe to recycle
    rg_display_drain();
}

void rg_gui_draw_text_box(int box_x, int box_y, int box_w, int box_h,
                          uint16_t bg_color, const char *text)
{
    int len = strlen(text);
    int char_w = 8 * current_font_scale;
    int text_w = len * char_w;
    int text_h = char_w;

    // Clamp box to at least text size
    if (box_w < text_w) box_w = text_w;
    if (box_h < text_h) box_h = text_h;

    // Quad-buffered pool to prevent DMA conflicts across rapid calls
    static uint16_t *tb_bufs[4] = {NULL, NULL, NULL, NULL};
    static int tb_sizes[4] = {0, 0, 0, 0};
    static int tb_idx = 0;
    tb_idx = (tb_idx + 1) % 4;

    int pixel_count = box_w * box_h;
    int needed = pixel_count * (int)sizeof(uint16_t);
    if (needed > tb_sizes[tb_idx]) {
        if (tb_bufs[tb_idx]) free(tb_bufs[tb_idx]);
        tb_bufs[tb_idx] = rg_gui_dma_malloc(needed);
        tb_sizes[tb_idx] = needed;
    }
    if (!tb_bufs[tb_idx]) return;
    uint16_t *buf = tb_bufs[tb_idx];

    uint16_t bg_sw = ((bg_color >> 8) | (bg_color << 8));
    uint16_t fg_sw = ((current_text_color >> 8) | (current_text_color << 8));

    // Fill entire box with background
    for (int i = 0; i < pixel_count; i++) buf[i] = bg_sw;

    // Center text horizontally and vertically within the box
    int text_ox = (box_w - text_w) / 2;
    int text_oy = (box_h - text_h) / 2;

    // Render scaled font glyphs
    for (int i = 0; i < len; i++) {
        uint8_t c = text[i];
        if (c > 127) c = '?';

        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[c][row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (1 << (7 - col)))) continue;
                for (int sy = 0; sy < current_font_scale; sy++) {
                    for (int sx = 0; sx < current_font_scale; sx++) {
                        int px = text_ox + i * char_w + col * current_font_scale + sx;
                        int py = text_oy + row * current_font_scale + sy;
                        if (px >= 0 && px < box_w && py >= 0 && py < box_h) {
                            buf[py * box_w + px] = fg_sw;
                        }
                    }
                }
            }
        }
    }

    rg_gui_send_dma_chunked(box_x, box_y, box_w, box_h, buf);
    // Drain so tb_buf slot is safe to recycle
    rg_display_drain();
}

void rg_gui_draw_text_line(int box_x, int box_y, int box_w, int box_h,
                           uint16_t bg_color, uint16_t text_color,
                           const char *text, int left_pad)
{
    int len = strlen(text);
    int char_w = 8 * current_font_scale;
    int text_h = char_w;
    if (box_h < text_h) box_h = text_h;

    static uint16_t *tl_bufs[4] = {NULL, NULL, NULL, NULL};
    static int tl_sizes[4] = {0, 0, 0, 0};
    static int tl_idx = 0;
    tl_idx = (tl_idx + 1) % 4;

    int pixel_count = box_w * box_h;
    int needed = pixel_count * (int)sizeof(uint16_t);
    if (needed > tl_sizes[tl_idx]) {
        if (tl_bufs[tl_idx]) free(tl_bufs[tl_idx]);
        tl_bufs[tl_idx] = rg_gui_dma_malloc(needed);
        tl_sizes[tl_idx] = needed;
    }
    if (!tl_bufs[tl_idx]) return;
    uint16_t *buf = tl_bufs[tl_idx];

    uint16_t bg_sw = ((bg_color >> 8) | (bg_color << 8));
    uint16_t fg_sw = ((text_color >> 8) | (text_color << 8));

    for (int i = 0; i < pixel_count; i++) buf[i] = bg_sw;

    int text_oy = (box_h - text_h) / 2;

    for (int i = 0; i < len; i++) {
        int px_start = left_pad + i * char_w;
        if (px_start + char_w <= 0) continue;
        if (px_start >= box_w) break;

        uint8_t c = text[i];
        if (c > 127) c = '?';

        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[c][row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (1 << (7 - col)))) continue;
                for (int sy = 0; sy < current_font_scale; sy++) {
                    for (int sx = 0; sx < current_font_scale; sx++) {
                        int px = px_start + col * current_font_scale + sx;
                        int py = text_oy + row * current_font_scale + sy;
                        if (px >= 0 && px < box_w && py >= 0 && py < box_h) {
                            buf[py * box_w + px] = fg_sw;
                        }
                    }
                }
            }
        }
    }

    rg_gui_send_dma_chunked(box_x, box_y, box_w, box_h, buf);
    rg_display_drain();
}

// Draw horizontal line for circle rasterization
static void draw_hline(int x1, int x2, int y, uint16_t color) {
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    rg_gui_draw_rect(x1, y, x2 - x1 + 1, 1, color);
}

void rg_gui_draw_filled_circle(int x0, int y0, int r)
{
    // Draw stroke (outline) as a slightly larger filled circle first
    if (current_stroke_width > 0) {
        int sr = r + current_stroke_width;
        int x = sr;
        int y = 0;
        int err = 0;
        while (x >= y) {
            draw_hline(x0 - x, x0 + x, y0 + y, current_stroke_color);
            draw_hline(x0 - x, x0 + x, y0 - y, current_stroke_color);
            draw_hline(x0 - y, x0 + y, y0 + x, current_stroke_color);
            draw_hline(x0 - y, x0 + y, y0 - x, current_stroke_color);

            if (err <= 0) {
                y += 1;
                err += 2 * y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    }

    // Draw inner fill
    int x = r;
    int y = 0;
    int err = 0;
    while (x >= y) {
        draw_hline(x0 - x, x0 + x, y0 + y, current_fill_color);
        draw_hline(x0 - x, x0 + x, y0 - y, current_fill_color);
        draw_hline(x0 - y, x0 + y, y0 + x, current_fill_color);
        draw_hline(x0 - y, x0 + y, y0 - x, current_fill_color);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

int rg_gui_get_text_width(const char *text)
{
    if (!text) return 0;
    return (int)strlen(text) * 8 * current_font_scale;
}

void rg_gui_draw_text_scaled(int x, int y, const char *text, uint16_t color, uint16_t bg_color)
{
    int len = strlen(text);
    if (len == 0) return;
    int char_w = 8 * current_font_scale;
    int total_w = len * char_w;
    int total_h = char_w;

    static uint16_t *ts_bufs[4] = {NULL, NULL, NULL, NULL};
    static int ts_sizes[4] = {0, 0, 0, 0};
    static int ts_idx = 0;
    ts_idx = (ts_idx + 1) % 4;

    int needed_size = total_w * total_h * (int)sizeof(uint16_t);
    if (needed_size > ts_sizes[ts_idx]) {
        if (ts_bufs[ts_idx]) free(ts_bufs[ts_idx]);
        ts_bufs[ts_idx] = rg_gui_dma_malloc(needed_size);
        ts_sizes[ts_idx] = needed_size;
    }
    if (!ts_bufs[ts_idx]) return;
    uint16_t *str_buf = ts_bufs[ts_idx];

    uint16_t fg_sw = ((color >> 8) | (color << 8));
    uint16_t bg_sw = ((bg_color >> 8) | (bg_color << 8));

    int pixel_count = total_w * total_h;
    for (int i = 0; i < pixel_count; i++) str_buf[i] = bg_sw;

    for (int i = 0; i < len; i++) {
        uint8_t c = text[i];
        if (c > 127) c = '?';
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[c][row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (1 << (7 - col)))) continue;
                for (int sy = 0; sy < current_font_scale; sy++) {
                    for (int sx = 0; sx < current_font_scale; sx++) {
                        int px = (i * char_w) + (col * current_font_scale + sx);
                        int py = (row * current_font_scale + sy);
                        if (px >= 0 && px < total_w && py >= 0 && py < total_h) {
                            str_buf[py * total_w + px] = fg_sw;
                        }
                    }
                }
            }
        }
    }
    rg_gui_send_dma_chunked(x, y, total_w, total_h, str_buf);
    rg_display_drain();
}
