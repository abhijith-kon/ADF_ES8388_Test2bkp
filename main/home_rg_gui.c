#include "home_rg_gui.h"
#include "rg_gui.h"
#include "rg_display.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
int home_ui_current_battery_pct = -1;
static float filtered_vbat = -1.0f;
static uint32_t last_adc_time = 0;

#define HOME_W          240 // Portrait
#define HOME_H          320
#define NUM_APPS        8
#define CENTER_X       (HOME_W / 2)
#define CENTER_Y       210
#define ICON_R_IDLE      22
#define ICON_R_FOCUSED   32

// Text area dimensions for flicker-free single-blit rendering
#define TIME_BOX_X      (CENTER_X - 70)
#define TIME_BOX_Y      23
#define TIME_BOX_W      140
#define TIME_BOX_H      28
#define DATE_BOX_X      (CENTER_X - 80)
#define DATE_BOX_Y      63
#define DATE_BOX_W      160
#define DATE_BOX_H      12
#define APPTEXT_BOX_X   (CENTER_X - 60)
#define APPTEXT_BOX_Y   (CENTER_Y - 11)
#define APPTEXT_BOX_W   120
#define APPTEXT_BOX_H   16

#include "app_icons.h"

typedef struct {
    const char *id;
    const char *name;
    const uint8_t *icon_bmp;
    int x_ofs;
    int y_ofs;
    // [Goal 4] Replaced icon_y_shift with independent icon_dx and icon_dy for easier positioning
    int icon_dx; 
    int icon_dy;
} home_app_t;

static const home_app_t home_apps[NUM_APPS] = {
    // Preserve existing layout, migrating icon_y_shift values to icon_dy
    {"APP_FILES",  "RSVP",          icon_files,   0, -75,  0,  0},
    {"APP_MUSIC",  "MUSIC",         icon_music,  53, -53,  0,  0},
    {"APP_RADIO",  "RADIO",         icon_radio,  75,   0,  0,  0},
    {"APP_WIFI",   "DOWNLOAD",      icon_wifi,   53,  53,  0, -2},
    {"APP_DSP",    "DSP",           icon_dsp,     0,  75,  0,  0},
    {"APP_VOL",    "SETTINGS",      icon_vol,   -53,  53,  0, -3},
    {"APP_ALARM",  "ALARM",         icon_alarm, -75,   0,  0, -1},
    {"APP_GAMES",  "GAMES",         icon_games, -53, -53,  0, -2},
};

typedef struct {
    uint8_t selected;
    // [Goal 1] Replaced anim_selected with previous_selected to track only the last active icon
    uint8_t previous_selected; 
    int icon_radius[NUM_APPS];
    char time_text[8];
    char date_text[32];
    char app_text[32];
    char moon_text[16];
    bool need_time_refresh;
    bool need_app_text_refresh;
    bool needs_redraw;
    bool force_all;
    bool icon_dirty[NUM_APPS];
} home_ui_t;

static home_ui_t home_ui;

static int get_battery_percentage(float voltage_mv) {
    if (voltage_mv >= 4200) return 100;
    if (voltage_mv >= 4100) return 90 + (voltage_mv - 4100) / 10.0f;
    if (voltage_mv >= 4000) return 80 + (voltage_mv - 4000) / 10.0f;
    if (voltage_mv >= 3900) return 60 + (voltage_mv - 3900) * 20.0f / 100.0f;
    if (voltage_mv >= 3800) return 40 + (voltage_mv - 3800) * 20.0f / 100.0f;
    if (voltage_mv >= 3700) return 20 + (voltage_mv - 3700) * 20.0f / 100.0f;
    if (voltage_mv >= 3600) return 5 + (voltage_mv - 3600) * 15.0f / 100.0f;
    if (voltage_mv >= 3300) return (voltage_mv - 3300) * 5.0f / 300.0f;
    return 0;
}

static void home_ui_update_battery(void) {
    if (!adc1_handle) return;
    int raw = 0;
    int total_raw = 0;
    int num_samples = 32;
    int valid_samples = 0;
    
    for (int i = 0; i < num_samples; i++) {
        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL_7, &raw) == ESP_OK) {
            total_raw += raw;
            valid_samples++;
        }
    }
    
    if (valid_samples > 0) {
        raw = total_raw / valid_samples;
        
        int voltage_mv = 0;
        if (cali_handle) {
            adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv);
        } else {
            voltage_mv = (raw * 3100) / 4095;
        }
        
        // V_pin = V_bat / 2
        float vbat_mv = voltage_mv * 2.0f;
        
        if (filtered_vbat < 0.0f) {
            filtered_vbat = vbat_mv;
        } else {
            filtered_vbat = filtered_vbat * 0.9f + vbat_mv * 0.1f;
        }
        
        int pct = get_battery_percentage(filtered_vbat);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        
        if (pct != home_ui_current_battery_pct) {
            home_ui_current_battery_pct = pct;
            home_ui.need_time_refresh = true; // Redraw battery along with time
        }
    }
}

static void home_ui_update_clock(void) {
    time_t now;
    time(&now);
    struct tm *tinfo = localtime(&now);
    snprintf(home_ui.time_text, sizeof(home_ui.time_text), "%02d:%02d", tinfo->tm_hour, tinfo->tm_min);
    
    const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int wday = tinfo->tm_wday;
    if (wday < 0 || wday > 6) wday = 0;
    int mon = tinfo->tm_mon;
    if (mon < 0 || mon > 11) mon = 0;
    snprintf(home_ui.date_text, sizeof(home_ui.date_text), "%s, %s %02d, %d", 
             days[wday], months[mon], tinfo->tm_mday, 1900 + tinfo->tm_year);
    
    int phase = (((long long)now - 592500) % 2551443) * 8 / 2551443;
    const char *phases[] = {"NEW", "WAX CRES", "1ST QTR", "WAX GIB", "FULL", "WAN GIB", "3RD QTR", "WAN CRES"};
    if (phase >= 0 && phase < 8) {
        snprintf(home_ui.moon_text, sizeof(home_ui.moon_text), "%s", phases[phase]);
    }

    home_ui.needs_redraw = true;
}

void home_ui_init(void) {
    memset(&home_ui, 0, sizeof(home_ui));
    home_ui.selected = 0;
    // Initialize previous_selected to the same as selected
    home_ui.previous_selected = 0; 
    for (int i = 0; i < NUM_APPS; i++) {
        home_ui.icon_radius[i] = ICON_R_IDLE;
        home_ui.icon_dirty[i] = true;
    }
    home_ui_update_clock();
    snprintf(home_ui.app_text, sizeof(home_ui.app_text), "%s", home_apps[0].name);
    home_ui.need_app_text_refresh = true;
    home_ui.needs_redraw = true;

    // Init ADC for battery on GPIO8 (ADC1_CH7)
    if (!adc1_handle) {
        adc_oneshot_unit_init_cfg_t init_config1 = {
            .unit_id = ADC_UNIT_1,
        };
        if (adc_oneshot_new_unit(&init_config1, &adc1_handle) == ESP_OK) {
            adc_oneshot_chan_cfg_t config = {
                .bitwidth = ADC_BITWIDTH_DEFAULT,
                .atten = ADC_ATTEN_DB_12,
            };
            adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_7, &config);
            
            adc_cali_curve_fitting_config_t cali_config = {
                .unit_id = ADC_UNIT_1,
                .chan = ADC_CHANNEL_7,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
            };
            if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) != ESP_OK) {
                printf("Failed to init ADC calibration scheme\n");
            }

            home_ui_update_battery();
        }
    }
}

void home_ui_set_selected(uint8_t idx) {
    if (idx >= NUM_APPS) return;
    if (home_ui.selected != idx) {
        home_ui.previous_selected = home_ui.selected;
        home_ui.selected = idx;
        home_ui.icon_dirty[home_ui.previous_selected] = true;
        home_ui.icon_dirty[home_ui.selected] = true;
    }
    snprintf(home_ui.app_text, sizeof(home_ui.app_text), "%s", home_apps[idx].name);
    home_ui.need_app_text_refresh = true;
    home_ui.needs_redraw = true;
}

void home_ui_move_left(void) {
    home_ui.previous_selected = home_ui.selected;
    home_ui.selected = (home_ui.selected + NUM_APPS - 1) % NUM_APPS;
    home_ui.icon_dirty[home_ui.previous_selected] = true;
    home_ui.icon_dirty[home_ui.selected] = true;
    snprintf(home_ui.app_text, sizeof(home_ui.app_text), "%s", home_apps[home_ui.selected].name);
    home_ui.need_app_text_refresh = true;
    home_ui.needs_redraw = true;
}

void home_ui_move_right(void) {
    home_ui.previous_selected = home_ui.selected;
    home_ui.selected = (home_ui.selected + 1) % NUM_APPS;
    home_ui.icon_dirty[home_ui.previous_selected] = true;
    home_ui.icon_dirty[home_ui.selected] = true;
    snprintf(home_ui.app_text, sizeof(home_ui.app_text), "%s", home_apps[home_ui.selected].name);
    home_ui.need_app_text_refresh = true;
    home_ui.needs_redraw = true;
}

int home_ui_get_selected(void) {
    return home_ui.selected;
}

void home_ui_tick(void) {
    static int last_min = -1;
    time_t now;
    time(&now);
    struct tm *tinfo = localtime(&now);
    
    // Refresh time every minute (when minute changes)
    if (tinfo && tinfo->tm_min != last_min) {
        home_ui.need_time_refresh = true;
        last_min = tinfo->tm_min;
    }

    uint32_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_adc_time > 1000) { // Check every 1 second
        last_adc_time = now_ms;
        home_ui_update_battery();
    }
}

void home_ui_update(void) {
    if (home_ui.need_time_refresh) {
        home_ui_update_clock();
    }

    for (int i = 0; i < NUM_APPS; i++) {
        int target_r = (i == home_ui.selected) ? ICON_R_FOCUSED : ICON_R_IDLE;
        int current_r = home_ui.icon_radius[i];
        
        if (current_r != target_r) {
            int step = (target_r > current_r) ? 4 : -4;
            current_r += step;
            if ((step > 0 && current_r >= target_r) || (step < 0 && current_r <= target_r)) {
                current_r = target_r;
            }
            home_ui.icon_radius[i] = current_r;
            home_ui.icon_dirty[i] = true;
            // Signal that we are actively animating so draw gets called
            home_ui.needs_redraw = true; 
        }
    }
}

void home_ui_force_redraw(void) {
    home_ui.needs_redraw = true;
    home_ui.force_all = true;
    home_ui.need_time_refresh = true;
    home_ui.need_app_text_refresh = true;
}



static void draw_app_node(int idx) {
    const home_app_t *a = &home_apps[idx];
    const int cx = CENTER_X + a->x_ofs;
    const int cy = CENTER_Y + a->y_ofs;
    const bool focused = (idx == home_ui.selected);
    const int r = home_ui.icon_radius[idx];

    // Always blit full NODE_SIZE to erase ghost outlines from prior animation states
    #define NODE_SIZE 68
    int blit_size = NODE_SIZE;
    int blit_half = blit_size / 2;

    // Quad buffering: 4 buffers to prevent DMA corruption across multiple frames
    static uint16_t node_bufs[4][NODE_SIZE * NODE_SIZE] __attribute__((aligned(4)));
    static int buf_idx = 0;
    buf_idx = (buf_idx + 1) % 4;
    uint16_t *node_buf = node_bufs[buf_idx];

    uint16_t bg_color = focused ? RG_COLOR_RGB(0x72, 0x09, 0xB7) : RG_COLOR_BLACK;
    uint16_t bg_swapped = ((bg_color >> 8) | (bg_color << 8));
    
    uint16_t border_color = focused ? RG_COLOR_WHITE : RG_COLOR_RGB(0x99, 0x99, 0x99);
    uint16_t border_swapped = ((border_color >> 8) | (border_color << 8));
    int border_w = focused ? 2 : 1;

    int r_sq = r * r;
    int inner_r_sq = (r - border_w) * (r - border_w);

    // Fill entire NODE_SIZE x NODE_SIZE buffer — black outside circle erases ghost rings
    for (int y = 0; y < blit_size; y++) {
        int dy = y - blit_half;
        int dy_sq = dy * dy;
        int row_offset = y * blit_size;
        
        for (int x = 0; x < blit_size; x++) {
            int dx = x - blit_half;
            int dist_sq = dx * dx + dy_sq;
            
            if (dist_sq <= r_sq) {
                if (dist_sq >= inner_r_sq) {
                    node_buf[row_offset + x] = border_swapped;
                } else {
                    node_buf[row_offset + x] = bg_swapped;
                }
            } else {
                node_buf[row_offset + x] = 0x0000; // Black
            }
        }
    }

    uint16_t fg_color = focused ? RG_COLOR_WHITE : RG_COLOR_RGB(0x99, 0x99, 0x99);
    uint16_t fg_swapped = ((fg_color >> 8) | (fg_color << 8));
    
    // Icon positioned relative to blit center
    int icon_start_x = blit_half - 12 + a->icon_dx;
    int icon_start_y = blit_half - 12 + a->icon_dy;
    
    for (int y = 0; y < 24; y++) {
        int buf_y = icon_start_y + y;
        if (buf_y < 0 || buf_y >= blit_size) continue;
        int row_offset = buf_y * blit_size;
        
        for (int x = 0; x < 24; x++) {
            int buf_x = icon_start_x + x;
            if (buf_x < 0 || buf_x >= blit_size) continue;
            
            int bit_idx = y * 24 + x;
            int byte_idx = bit_idx / 8;
            int bit_rem = 7 - (bit_idx % 8);
            if (a->icon_bmp[byte_idx] & (1 << bit_rem)) {
                node_buf[row_offset + buf_x] = fg_swapped;
            }
        }
    }

    rg_display_write(cx - blit_half, cy - blit_half, blit_size, blit_size, blit_size * 2, node_buf);
    rg_display_drain();
}

void home_ui_draw(void) {
    static bool first_draw = true;
    if (first_draw) {
        rg_gui_clear(RG_COLOR_BLACK);
        first_draw = false;
        
        // Force draw all icons initially, selected icon last
        for (int i = 0; i < NUM_APPS; i++) {
            if (i != home_ui.selected) draw_app_node(i);
        }
        draw_app_node(home_ui.selected);
        
        rg_gui_set_font_size(14);
        rg_gui_set_text_color(RG_COLOR_WHITE);
        rg_gui_draw_text_box(APPTEXT_BOX_X, APPTEXT_BOX_Y, APPTEXT_BOX_W, APPTEXT_BOX_H,
                             RG_COLOR_BLACK, home_ui.app_text);
        home_ui.needs_redraw = false;
        home_ui.need_time_refresh = true; // Ensure time gets drawn on the next tick
        return;
    }

    if (!home_ui.needs_redraw) return;

    // Throttle redraws to ~50 FPS (20ms) to allow SPI DMA queue to empty.
    // This prevents overwriting node_bufs while they are still in flight!
    static int64_t last_draw_time = 0;
    int64_t now = esp_timer_get_time() / 1000;
    if (now - last_draw_time < 20) {
        return;
    }
    last_draw_time = now;

    // [Goal 2] Only redraw time area when necessary
    if (home_ui.need_time_refresh) {
        rg_gui_set_font_size(30);
        rg_gui_set_text_color(RG_COLOR_WHITE);
        rg_gui_draw_text_box(TIME_BOX_X, TIME_BOX_Y, TIME_BOX_W, TIME_BOX_H,
                             RG_COLOR_BLACK, home_ui.time_text);
        rg_gui_set_font_size(12);
        rg_gui_set_text_color(RG_COLOR_RGB(0xAA, 0xAA, 0xAA));
        rg_gui_draw_text_box(DATE_BOX_X, DATE_BOX_Y, DATE_BOX_W, DATE_BOX_H,
                             RG_COLOR_BLACK, home_ui.date_text);
                             
        if (home_ui_current_battery_pct >= 0) {
            int bx = 210;
            int by = TIME_BOX_Y - 15;
            int bw = 20;
            int bh = 10;
            
            rg_gui_draw_rect(bx, by, bw, bh, RG_COLOR_RGB(150, 150, 150)); // Battery body
            rg_gui_draw_rect(bx + bw, by + 2, 2, 6, RG_COLOR_RGB(150, 150, 150)); // Terminal
            rg_gui_draw_rect(bx + 1, by + 1, bw - 2, bh - 2, RG_COLOR_BLACK); // Inner bg
            
            int level_w = ((bw - 2) * home_ui_current_battery_pct) / 100;
            if (level_w > 0) {
                uint16_t color = RG_COLOR_RGB(0, 255, 0); // Green
                if (home_ui_current_battery_pct <= 20) color = RG_COLOR_RGB(255, 0, 0); // Red
                else if (home_ui_current_battery_pct <= 50) color = RG_COLOR_RGB(255, 255, 0); // Yellow
                
                rg_gui_draw_rect(bx + 1, by + 1, level_w, bh - 2, color);
            }
            
            // Draw moon phase
            rg_gui_set_font_size(8);
            rg_gui_set_text_color(RG_COLOR_RGB(200, 200, 220));
            rg_gui_draw_text_box(bx - 65, by + 1, 60, 12, RG_COLOR_BLACK, home_ui.moon_text);
        }
        
        home_ui.need_time_refresh = false;
    }

    // [Goal 2] Only redraw the specific apps that are affected, always drawing selected icon last!
    if (home_ui.force_all) {
        for (int i = 0; i < NUM_APPS; i++) {
            if (i != home_ui.selected) {
                draw_app_node(i);
                home_ui.icon_dirty[i] = false;
            }
        }
        draw_app_node(home_ui.selected);
        home_ui.icon_dirty[home_ui.selected] = false;
        home_ui.force_all = false;
    } else {
        bool unselected_drawn = false;
        for (int i = 0; i < NUM_APPS; i++) {
            if (i != home_ui.selected && home_ui.icon_dirty[i]) {
                draw_app_node(i);
                home_ui.icon_dirty[i] = false;
                unselected_drawn = true;
            }
        }
        // If any unselected icon redrew its 68x68 black bounding box, its corner may have overlapped
        // the selected icon. Always redraw the selected icon on top if any unselected icon drew or if it is dirty.
        if (unselected_drawn || home_ui.icon_dirty[home_ui.selected]) {
            draw_app_node(home_ui.selected);
            home_ui.icon_dirty[home_ui.selected] = false;
        }
    }

    // [Goal 2] Only redraw text box when the selected app changes
    if (home_ui.need_app_text_refresh) {
        rg_gui_set_font_size(14);
        rg_gui_set_text_color(RG_COLOR_WHITE);
        rg_gui_draw_text_box(APPTEXT_BOX_X, APPTEXT_BOX_Y, APPTEXT_BOX_W, APPTEXT_BOX_H,
                             RG_COLOR_BLACK, home_ui.app_text);
        home_ui.need_app_text_refresh = false;
    }
    
    // Clear flag; if animation is still ongoing, home_ui_update will set it back to true
    home_ui.needs_redraw = false;
}
