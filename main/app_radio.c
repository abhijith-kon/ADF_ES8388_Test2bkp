#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "i2c_bus.h"
#include "es8388.h"
#include "input_manager.h"
#include "app_radio.h"

static const char *TAG = "APP_RADIO";

#define SCREEN_W         240
#define SCREEN_H         320

// Color Palette
#define BG_COLOR         RG_COLOR_RGB(10, 10, 18)
#define HEADER_BG        RG_COLOR_RGB(25, 20, 40)
#define HEADER_FG        RG_COLOR_RGB(255, 180, 50)
#define BOX_BG           RG_COLOR_RGB(18, 18, 30)
#define BOX_BORDER       RG_COLOR_RGB(0, 200, 255)
#define TEXT_COLOR       RG_COLOR_WHITE
#define MUTED_COLOR      RG_COLOR_RGB(255, 60, 60)
#define FAV_SELECTED_BG  RG_COLOR_RGB(160, 80, 240)
#define FAV_SELECTED_FG  RG_COLOR_WHITE
#define FAV_NORMAL_BG    BG_COLOR
#define FAV_NORMAL_FG    RG_COLOR_RGB(180, 180, 190)

// Favorite Channels requested by user
static const float fav_channels[] = {
    93.5f,
    91.9f,
    92.7f,
    103.6f,
    102.8f,
    104.6f
};
static const char *fav_names[] = {
    "Red FM",
    "Radio City",
    "BIG FM",
    "Fav Station 4",
    "Fav Station 5",
    "Fav Station 6"
};
#define NUM_FAV_CHANNELS (sizeof(fav_channels) / sizeof(fav_channels[0]))

static bool in_radio_ui = false;
static i2c_bus_handle_t radio_i2c_bus = NULL;
static int fav_idx = 0;
static float current_freq = 93.5f;
static int radio_vol = 80;
static bool radio_muted = false;
static int64_t last_status_update = 0;
static int last_rssi = 50;
static bool last_stereo = true;

static void draw_radio_ui(void);
static void radio_tune(float freq);

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
                box_buf[py * w + px] = 0x0000; // Black outside corners
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

static void draw_radio_ui(void)
{
    if (!in_radio_ui) return;
    rg_gui_draw_rect(0, 0, SCREEN_W, SCREEN_H, BG_COLOR);

    // Header
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(HEADER_FG);
    rg_gui_draw_text_box(0, 5, SCREEN_W, 22, BG_COLOR, "FM RADIO - RDA5657");
    rg_gui_draw_rect(0, 30, SCREEN_W, 2, RG_COLOR_RGB(60, 45, 80));

    // Tuner Display Box
    draw_rounded_box(6, 36, 228, 94, 10, BOX_BG, BOX_BORDER, 2);

    // Frequency text
    char freq_str[32];
    snprintf(freq_str, sizeof(freq_str), "%5.1f MHz", current_freq);
    rg_gui_set_font_size(24);
    rg_gui_set_text_color(TEXT_COLOR);
    int fw = rg_gui_get_text_width(freq_str);
    rg_gui_draw_text_scaled((SCREEN_W - fw) / 2, 48, freq_str, TEXT_COLOR, BOX_BG);

    // Mode / Station title
    char mode_str[32];
    if (fav_idx >= 0 && fav_idx < (int)NUM_FAV_CHANNELS) {
        snprintf(mode_str, sizeof(mode_str), "[ FAV %d / %d : %s ]", fav_idx + 1, (int)NUM_FAV_CHANNELS, fav_names[fav_idx]);
    } else {
        snprintf(mode_str, sizeof(mode_str), "[ MANUAL TUNE ]");
    }
    rg_gui_set_font_size(8);
    rg_gui_set_text_color(RG_COLOR_RGB(255, 220, 100));
    int mw = rg_gui_get_text_width(mode_str);
    rg_gui_draw_text((SCREEN_W - mw) / 2, 84, mode_str, RG_COLOR_RGB(255, 220, 100), BOX_BG);

    // Status bar (Volume, RSSI, Stereo)
    char stat_str[64];
    if (radio_muted) {
        snprintf(stat_str, sizeof(stat_str), "VOL: MUTED | RSSI: %02d | %s", last_rssi, last_stereo ? "STEREO" : "MONO");
        rg_gui_set_text_color(MUTED_COLOR);
        int sw = rg_gui_get_text_width(stat_str);
        rg_gui_draw_text((SCREEN_W - sw) / 2, 106, stat_str, MUTED_COLOR, BOX_BG);
    } else {
        snprintf(stat_str, sizeof(stat_str), "VOL: %d%% | RSSI: %02d | %s", radio_vol, last_rssi, last_stereo ? "STEREO" : "MONO");
        rg_gui_set_text_color(RG_COLOR_RGB(100, 255, 180));
        int sw = rg_gui_get_text_width(stat_str);
        rg_gui_draw_text((SCREEN_W - sw) / 2, 106, stat_str, RG_COLOR_RGB(100, 255, 180), BOX_BG);
    }

    // Favorite Channels List
    rg_gui_set_font_size(8);
    int list_y = 142;
    for (int i = 0; i < (int)NUM_FAV_CHANNELS; i++) {
        uint16_t row_bg = (fav_idx == i) ? FAV_SELECTED_BG : FAV_NORMAL_BG;
        uint16_t row_fg = (fav_idx == i) ? FAV_SELECTED_FG : FAV_NORMAL_FG;

        if (fav_idx == i) {
            draw_rounded_box(10, list_y + i * 26, 220, 22, 6, row_bg, row_bg, 0);
        }

        char row_str[40];
        snprintf(row_str, sizeof(row_str), "%s %5.1f MHz  -  %s", (fav_idx == i) ? ">" : " ", fav_channels[i], fav_names[i]);
        rg_gui_draw_text(18, list_y + i * 26 + 7, row_str, row_fg, row_bg);
    }

    // Per user request item 3: NO navigation instructions are drawn on any page!
    rg_display_drain();
}

static void radio_tune(float freq)
{
    if (!radio_i2c_bus) return;
    current_freq = freq;

    int chan = (int)((freq - 87.0f) * 10.0f + 0.5f);
    if (chan < 0) chan = 0;
    if (chan > 210) chan = 210;

    uint16_t reg02 = radio_muted ? 0x9001 : 0xD001; // DHIZ=1, DMUTE=1(unmuted), BASS=1, ENABLE=1
    uint16_t reg03 = (uint16_t)((chan << 6) | 0x0010); // TUNE=1, BAND=00 (87-108), SPACE=00 (100kHz)
    
    int vol_level = (radio_vol * 15) / 100;
    if (vol_level < 0) vol_level = 0;
    if (vol_level > 15) vol_level = 15;
    uint16_t reg05 = (uint16_t)(0x880F | ((vol_level & 0x0F) << 4));

    // Method 1: Random Access Mode (I2C Address 0x11, shifted left is 0x22)
    uint8_t reg = 0x02;
    uint8_t data02[2] = { (uint8_t)(reg02 >> 8), (uint8_t)(reg02 & 0xFF) };
    i2c_bus_write_bytes(radio_i2c_bus, (0x11 << 1), &reg, 1, data02, 2);

    reg = 0x03;
    uint8_t data03[2] = { (uint8_t)(reg03 >> 8), (uint8_t)(reg03 & 0xFF) };
    i2c_bus_write_bytes(radio_i2c_bus, (0x11 << 1), &reg, 1, data03, 2);

    reg = 0x05;
    uint8_t data05[2] = { (uint8_t)(reg05 >> 8), (uint8_t)(reg05 & 0xFF) };
    i2c_bus_write_bytes(radio_i2c_bus, (0x11 << 1), &reg, 1, data05, 2);

    // Method 2: Sequential Write Mode (I2C Address 0x10, shifted left is 0x20)
    uint8_t seq_buf[8] = {
        (uint8_t)(reg02 >> 8), (uint8_t)(reg02 & 0xFF),
        (uint8_t)(reg03 >> 8), (uint8_t)(reg03 & 0xFF),
        0x00, 0x00,
        (uint8_t)(reg05 >> 8), (uint8_t)(reg05 & 0xFF)
    };
    i2c_bus_write_data(radio_i2c_bus, (0x10 << 1), seq_buf, 8);

    ESP_LOGI(TAG, "Tuned RDA5657/RDA5807 to %.1f MHz (chan=%d, vol=%d%%, mute=%d)", freq, chan, radio_vol, radio_muted);
    draw_radio_ui();
}

void app_radio_start(void)
{
    if (in_radio_ui) return;
    in_radio_ui = true;

    // Connect to shared I2C bus (SDA: GPIO 4, SCL: GPIO 5)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 4,
        .scl_io_num = 5,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    if (!radio_i2c_bus) {
        radio_i2c_bus = i2c_bus_create(I2C_NUM_0, &conf);
    }

    // Enable ES8388 analog input mixer (LIN1/RIN1 and LIN2/RIN2) to pass through radio audio to speakers/headphones
    es8388_write_reg(ES8388_DACCONTROL17, 0x98);
    es8388_write_reg(ES8388_DACCONTROL20, 0x98);

    radio_muted = false;
    fav_idx = 0;
    current_freq = fav_channels[0];
    last_status_update = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Radio App Started");
    radio_tune(current_freq);
}

void app_radio_stop(void)
{
    if (!in_radio_ui) return;
    in_radio_ui = false;

    if (radio_i2c_bus) {
        // Power down RDA5807 / RDA5657 (ENABLE bit = 0)
        uint8_t reg = 0x02;
        uint8_t data[2] = {0x00, 0x00};
        i2c_bus_write_bytes(radio_i2c_bus, (0x11 << 1), &reg, 1, data, 2);
        i2c_bus_write_data(radio_i2c_bus, (0x10 << 1), data, 2);
    }

    // Restore ES8388 mixer to default DAC only (0x80) to eliminate static noise in other apps
    es8388_write_reg(ES8388_DACCONTROL17, 0x80);
    es8388_write_reg(ES8388_DACCONTROL20, 0x80);

    ESP_LOGI(TAG, "Radio App Stopped");
}

static void set_es8388_volume(int vol)
{
    uint8_t reg_val = (uint8_t)(((100 - vol) * 192) / 100);
    while (es8388_write_reg(ES8388_DACCONTROL4, reg_val) != ESP_OK) { vTaskDelay(1); }
    while (es8388_write_reg(ES8388_DACCONTROL5, reg_val) != ESP_OK) { vTaskDelay(1); }
    ESP_LOGI(TAG, "Radio Volume set to %d (reg=0x%02x)", vol, reg_val);
}

void app_radio_handle_input(int button_event)
{
    if (!in_radio_ui) return;

    switch (button_event) {
        case BTN_LEFT:
            fav_idx = (fav_idx + (int)NUM_FAV_CHANNELS - 1) % (int)NUM_FAV_CHANNELS;
            radio_tune(fav_channels[fav_idx]);
            break;
        case BTN_RIGHT:
            fav_idx = (fav_idx + 1) % (int)NUM_FAV_CHANNELS;
            radio_tune(fav_channels[fav_idx]);
            break;
        case BTN_UP:
            current_freq += 0.1f;
            if (current_freq > 108.0f) current_freq = 87.0f;
            fav_idx = -1;
            for (int i = 0; i < (int)NUM_FAV_CHANNELS; i++) {
                if (fabsf(current_freq - fav_channels[i]) < 0.05f) {
                    fav_idx = i;
                    current_freq = fav_channels[i];
                    break;
                }
            }
            radio_tune(current_freq);
            break;
        case BTN_DOWN:
            current_freq -= 0.1f;
            if (current_freq < 87.0f) current_freq = 108.0f;
            fav_idx = -1;
            for (int i = 0; i < (int)NUM_FAV_CHANNELS; i++) {
                if (fabsf(current_freq - fav_channels[i]) < 0.05f) {
                    fav_idx = i;
                    current_freq = fav_channels[i];
                    break;
                }
            }
            radio_tune(current_freq);
            break;
        case BTN_B:
        case BTN_ENTER:
            radio_muted = !radio_muted;
            radio_tune(current_freq);
            break;
        case BTN_VOL_UP:
            radio_vol = (radio_vol + 10 > 100) ? 100 : radio_vol + 10;
            set_es8388_volume(radio_vol);
            radio_tune(current_freq);
            break;
        case BTN_VOL_DOWN:
            radio_vol = (radio_vol - 10 < 0) ? 0 : radio_vol - 10;
            set_es8388_volume(radio_vol);
            radio_tune(current_freq);
            break;
        default:
            break;
    }
}

void app_radio_tick(void)
{
    if (!in_radio_ui || !radio_i2c_bus) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_status_update >= 1500) {
        last_status_update = now_ms;

        uint8_t regA = 0x0A;
        uint8_t dataA[2] = {0};
        uint8_t regB = 0x0B;
        uint8_t dataB[2] = {0};

        esp_err_t errA = i2c_bus_read_bytes(radio_i2c_bus, (0x11 << 1), &regA, 1, dataA, 2);
        esp_err_t errB = i2c_bus_read_bytes(radio_i2c_bus, (0x11 << 1), &regB, 1, dataB, 2);

        bool changed = false;
        if (errA == ESP_OK && errB == ESP_OK) {
            uint16_t valA = (dataA[0] << 8) | dataA[1];
            uint16_t valB = (dataB[0] << 8) | dataB[1];
            bool st = (valA & (1 << 10)) != 0;
            int rs = (valB >> 9) & 0x7F;
            if (st != last_stereo || rs != last_rssi) {
                last_stereo = st;
                last_rssi = rs;
                changed = true;
            }
        }
        if (changed) {
            draw_radio_ui();
        }
    }
}

bool app_radio_is_active(void)
{
    return in_radio_ui;
}
