#ifndef RG_GUI_H
#define RG_GUI_H

#include <stdint.h>

#define RG_COLOR_RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define RG_COLOR_WHITE 0xFFFF
#define RG_COLOR_BLACK 0x0000

void rg_gui_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg_color);
void rg_gui_draw_rect(int x, int y, int w, int h, uint16_t color);
void rg_gui_draw_image(int x, int y, int w, int h, const uint16_t *data);
void rg_gui_clear(uint16_t color);

void rg_gui_set_font_size(int size_px);
void rg_gui_set_text_color(uint16_t color);
#ifndef RG_GUI_H
#define RG_GUI_H

#include <stdint.h>

#define RG_COLOR_RGB(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define RG_COLOR_WHITE 0xFFFF
#define RG_COLOR_BLACK 0x0000

void rg_gui_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg_color);
void rg_gui_draw_rect(int x, int y, int w, int h, uint16_t color);
void rg_gui_draw_image(int x, int y, int w, int h, const uint16_t *data);
void rg_gui_clear(uint16_t color);

void rg_gui_set_font_size(int size_px);
void rg_gui_set_text_color(uint16_t color);
void rg_gui_set_fill_color(uint16_t color);
void rg_gui_set_stroke_color(uint16_t color);
void rg_gui_set_stroke_width(int width);

void rg_gui_draw_text_center(int x, int y, const char *text);
void rg_gui_draw_text_box(int box_x, int box_y, int box_w, int box_h,
                          uint16_t bg_color, const char *text);
void rg_gui_draw_text_line(int box_x, int box_y, int box_w, int box_h,
                           uint16_t bg_color, uint16_t text_color,
                           const char *text, int left_pad);
void rg_gui_draw_filled_circle(int x0, int y0, int r);

#endif
