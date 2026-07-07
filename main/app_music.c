#include "app_music.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "esp_decoder.h"
#include "mp3_decoder.h"
#include "flac_decoder.h"
#include "aac_decoder.h"
#include "wav_decoder.h"
#include "audio_event_iface.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "es8388.h"
#include "audio_volume.h"
#include <stdio.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp32s3/rom/tjpgd.h"

static const char *TAG = "APP_MUSIC";

#define MAX_PLAYLIST_FILES 1000
static char **playlist = NULL;
static int total_tracks = 0;
static int current_track = 0;
static bool music_initialized = false;
static SemaphoreHandle_t playlist_mutex = NULL;
static volatile bool scan_in_progress = false;
static int last_known_total_tracks = 0;

static audio_pipeline_handle_t pipeline = NULL;
static audio_element_handle_t fatfs_stream_reader = NULL;
static audio_element_handle_t i2s_stream_writer = NULL;
static audio_element_handle_t mp3_decoder = NULL;
static bool is_playing = false;
static bool pipeline_has_run = false;
static audio_event_iface_handle_t evt = NULL;
static audio_hal_handle_t music_hal_handle = NULL;
static int current_volume = 80;

// ---- Layout Constants ----
#define SCREEN_W         240
#define MUSIC_BG         RG_COLOR_BLACK

#define TITLE_Y          2
#define TITLE_H          20

#define SEP_COLOR        RG_COLOR_RGB(80, 80, 80)
#define SEP1_Y           24

#define LIST_Y           28
#define LIST_ITEM_H      26
#define VISIBLE_ITEMS    6
#define HIGHLIGHT_BG     RG_COLOR_RGB(20, 60, 180)
#define PLAYING_CLR      RG_COLOR_RGB(50, 255, 50)
#define PAUSED_CLR       RG_COLOR_RGB(255, 200, 50)

#define SEP2_Y           256

#define MINI_BOX_X       6
#define MINI_BOX_Y       258
#define MINI_BOX_W       228
#define MINI_BOX_H       44
#define MINI_BOX_BG      RG_COLOR_RGB(140, 60, 220)
#define MINI_BOX_BORDER  RG_COLOR_RGB(180, 100, 255)

#define MINI_TRACK_X     12
#define MINI_TRACK_Y     272
#define MINI_TRACK_W     216
#define MINI_TRACK_H     16

// ---- UI State ----
static int selected_index = 0;
static int view_start = 0;

static int scroll_char_offset = 0;
static int64_t scroll_timer = 0;
#define SCROLL_INTERVAL_MS 150
#define MAX_NAME_CHARS     27

static size_t current_track_bytes = 0;

static bool list_full_dirty = true;
static bool scroll_only_dirty = false;
static bool player_dirty = true;
static bool slot_dirty[6] = {false};
static int prev_selected = -1;

// Mini player scroll state
static int mini_scroll_char_offset = 0;
static int64_t mini_scroll_timer = 0;
#define MINI_SCROLL_INTERVAL_MS 200
#define MINI_MAX_NAME_CHARS     27
static bool mini_player_scroll_dirty = false;

// ---- Player UI State & Visualizer ----
static bool in_player_ui = false;
static bool player_ui_top_dirty = false;
static int player_scroll_char_offset = 0;
static int64_t player_scroll_timer = 0;
static int64_t last_vis_time = 0;
static int64_t last_time_update = 0;

// Volume bar state
static bool vol_bar_visible = false;
static int64_t vol_bar_timer = 0;

// Audio format info
static int info_sample_rate = 44100;
static int info_bits = 16;
static int info_channels = 2;
static int info_bitrate = 320;
static char info_format_str[16] = "MP3";
static char info_artist_str[64] = "Unknown Artist";
static char info_title_str[128] = "";
static int64_t saved_byte_pos = 0;
static bool is_shuffle = false;
static bool show_thumbnail = false;
static uint32_t current_apic_offset = 0;
static uint32_t current_apic_size = 0;

// FFT visualizer state (48 radial lines)
#define NUM_FFT_BANDS 48
static float fft_val[NUM_FFT_BANDS] = {0};
static float fft_vel[NUM_FFT_BANDS] = {0};
static float fft_freq[NUM_FFT_BANDS] = {0};
static float fft_phase[NUM_FFT_BANDS] = {0};
static bool fft_init = false;
static uint16_t *vis_buf = NULL;
static ringbuf_handle_t fft_ringbuf = NULL;

static void init_audio_pipeline(void);
static void draw_player_ui_full(void);
static void draw_player_top_area(bool full_redraw);
static void draw_player_title(void);
static void draw_player_metadata(void);
static void draw_player_timer(void);
static void draw_player_visualizer(void);
static void draw_player_thumbnail(void);
static void draw_volume_bar(void);
static float get_playback_progress(void);

static void init_fft_state(void)
{
    if (fft_init) return;
    for (int i = 0; i < NUM_FFT_BANDS; i++) {
        fft_val[i] = 2.0f;
        fft_vel[i] = 0.0f;
        fft_freq[i] = 1.5f + (float)(esp_random() % 30) * 0.1f;
        fft_phase[i] = (float)(esp_random() % 628) * 0.01f;
    }
    fft_init = true;
}

static uint16_t get_rainbow_color(int i, int total)
{
    float angle = (float)i / (float)total * 6.2831853f;
    int r = (int)(127.0f * sinf(angle) + 128.0f);
    int g = (int)(127.0f * sinf(angle + 2.094395f) + 128.0f);
    int b = (int)(127.0f * sinf(angle + 4.188790f) + 128.0f);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return RG_COLOR_RGB(r, g, b);
}

static void draw_line_in_buf(uint16_t *buf, int w, int h, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    uint16_t color_sw = (uint16_t)((color >> 8) | (color << 8));

    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            buf[y0 * w + x0] = color_sw;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static bool is_supported_audio_file(const char *name)
{
    size_t len = strlen(name);
    if (len > 4) {
        const char *ext = name + len - 4;
        if (strcasecmp(ext, ".mp3") == 0 ||
            strcasecmp(ext, ".wav") == 0 ||
            strcasecmp(ext, ".aac") == 0 ||
            strcasecmp(ext, ".m4a") == 0) {
            return true;
        }
    }
    if (len > 5) {
        const char *ext = name + len - 5;
        if (strcasecmp(ext, ".flac") == 0) {
            return true;
        }
    }
    return false;
}

static void sd_card_scan_task(void *arg)
{
    scan_in_progress = true;
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sdcard");
        scan_in_progress = false;
        vTaskDelete(NULL);
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (is_supported_audio_file(ent->d_name)) {
            if (playlist_mutex) xSemaphoreTake(playlist_mutex, portMAX_DELAY);
            if (total_tracks < MAX_PLAYLIST_FILES) {
                playlist[total_tracks] = strdup(ent->d_name);
                total_tracks++;
            }
            int cur_count = total_tracks;
            if (playlist_mutex) xSemaphoreGive(playlist_mutex);

            if (cur_count == 1) {
                init_audio_pipeline();
            }
            if (cur_count >= MAX_PLAYLIST_FILES) {
                break;
            }
            if (cur_count % 10 == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Background scan finished. Total audio tracks found: %d", total_tracks);
    scan_in_progress = false;
    vTaskDelete(NULL);
}

static int decoder_write_cb(audio_element_handle_t el, char *buffer, int len, TickType_t ticks_to_wait, void *ctx)
{
    static int cnt = 0;
    if (++cnt % 100 == 0) {
        ESP_LOGI(TAG, "decoder callback %d bytes", len);
    }
    if (!show_thumbnail && fft_ringbuf && len > 0) {
        int avail_fill = rb_bytes_filled(fft_ringbuf);
        if (avail_fill + len > 4000) {
            char dummy[512];
            while (rb_bytes_filled(fft_ringbuf) + len > 4000) {
                if (rb_read(fft_ringbuf, dummy, sizeof(dummy), 0) <= 0) break;
            }
        }
        rb_write(fft_ringbuf, buffer, len, 0);
    }
    ringbuf_handle_t out_rb = (ringbuf_handle_t)ctx;
    if (out_rb) {
        return rb_write(out_rb, buffer, len, ticks_to_wait);
    }
    return len;
}

// ---- Audio Pipeline Init ----
static void init_audio_pipeline(void)
{
    ESP_LOGI(TAG, "Initializing Audio Pipeline with Auto-Decoder (MP3, FLAC, AAC, WAV, M4A)...");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.out_rb_size = 32 * 1024;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size = 32 * 1024;
    i2s_cfg.chan_cfg.dma_desc_num = 6;
    i2s_cfg.chan_cfg.dma_frame_num = 480;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    if (!fft_ringbuf) {
        fft_ringbuf = rb_create(4096, 1);
    }

    audio_decoder_t auto_decode[] = {
        DEFAULT_ESP_MP3_DECODER_CONFIG(),
        DEFAULT_ESP_FLAC_DECODER_CONFIG(),
        DEFAULT_ESP_AAC_DECODER_CONFIG(),
        DEFAULT_ESP_WAV_DECODER_CONFIG(),
        DEFAULT_ESP_M4A_DECODER_CONFIG(),
    };
    esp_decoder_cfg_t auto_dec_cfg = DEFAULT_ESP_DECODER_CONFIG();
    mp3_decoder = esp_decoder_init(&auto_dec_cfg, auto_decode, sizeof(auto_decode) / sizeof(auto_decode[0]));

    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, mp3_decoder, "dec");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    const char *link_tag[3] = {"file", "dec", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ringbuf_handle_t dec_out_rb = audio_element_get_output_ringbuf(mp3_decoder);
    if (dec_out_rb) {
        audio_element_set_write_cb(mp3_decoder, decoder_write_cb, (void *)dec_out_rb);
        ESP_LOGI(TAG, "Attached write callback after decoder to tap real PCM audio for FFT visualizer.");
    }

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);
}

// ---- Navigation ----
static void format_clean_song_name(const char *raw, char *out, size_t max_len)
{
    if (!raw || !out || max_len == 0) return;
    strncpy(out, raw, max_len - 1);
    out[max_len - 1] = '\0';
    char *ext = strrchr(out, '.');
    if (ext && (strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".flac") == 0 ||
                strcasecmp(ext, ".wav") == 0 || strcasecmp(ext, ".m4a") == 0 ||
                strcasecmp(ext, ".aac") == 0)) {
        *ext = '\0';
    }
}

static void draw_rounded_box(int x, int y, int w, int h, int r, uint16_t fill_color, uint16_t border_color, int border_width)
{
    static uint16_t *box_buf = NULL;
    static int box_buf_cap = 0;
    int needed = w * h * (int)sizeof(uint16_t);
    if (needed > box_buf_cap) {
        if (box_buf) free(box_buf);
        box_buf = heap_caps_malloc(needed, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
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
                box_buf[py * w + px] = 0x0000; // Black background outside corners
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

static void ensure_cursor_visible(void)
{
    // In iPod wheel design, the highlight is always fixed at row index 2 (out of 0..5)
    view_start = selected_index - 2;
}

// ---- Progress ----
static float get_playback_progress(void)
{
    if (!is_playing || !fatfs_stream_reader || current_track_bytes == 0) return 0.0f;
    audio_element_info_t info = {0};
    audio_element_getinfo(fatfs_stream_reader, &info);
    if (info.byte_pos <= 0) return 0.0f;
    float progress = (float)info.byte_pos / (float)current_track_bytes;
    if (progress > 1.0f) progress = 1.0f;
    return progress;
}

// ---- Drawing ----
static void draw_list_item(int slot)
{
    int track_idx = view_start + slot;
    int y = 31 + slot * 33;
    bool is_sel = (slot == 2); // Highlight never moves; always at row 2

    if (track_idx < 0 || track_idx >= total_tracks) {
        if (is_sel) {
            draw_rounded_box(6, y + 1, 228, 31, 6, RG_COLOR_RGB(70, 150, 255), RG_COLOR_RGB(70, 150, 255), 0);
            rg_gui_set_font_size(8);
            rg_gui_draw_text_line(12, y + 12, 216, 16, RG_COLOR_RGB(70, 150, 255), RG_COLOR_WHITE, "  No Track", 8);
        } else {
            rg_gui_draw_rect(6, y, 228, 33, RG_COLOR_BLACK);
            if (slot < 5 && slot != 1 && slot != 2) {
                rg_gui_draw_rect(14, y + 32, 212, 1, RG_COLOR_RGB(40, 40, 45));
            }
        }
        return;
    }

    uint16_t bg = is_sel ? RG_COLOR_RGB(70, 150, 255) : RG_COLOR_BLACK;
    uint16_t fg = RG_COLOR_WHITE;

    const char *raw_name = playlist[track_idx];
    char clean_name[128];
    format_clean_song_name(raw_name, clean_name, sizeof(clean_name));
    int name_len = strlen(clean_name);

    int ofs = is_sel ? scroll_char_offset : 0;
    int max_ofs = name_len - MAX_NAME_CHARS;
    if (max_ofs < 0) max_ofs = 0;
    if (ofs > max_ofs) ofs = max_ofs;

    char display[128];
    snprintf(display, sizeof(display), "%s", clean_name + ofs);

    if (is_sel) {
        draw_rounded_box(6, y + 1, 228, 31, 6, bg, bg, 0);
    } else {
        rg_gui_draw_rect(6, y, 228, 33, RG_COLOR_BLACK);
    }

    int disp_len = strlen(display);
    int text_w = disp_len * 8;
    int left_pad = (228 - text_w) / 2;
    if (left_pad < 0) left_pad = 0;

    rg_gui_set_font_size(8);
    rg_gui_draw_text_line(6, y + 12, 228, 16, bg, fg, display, left_pad);

    if (slot < 5 && slot != 1 && slot != 2) {
        rg_gui_draw_rect(14, y + 32, 212, 1, RG_COLOR_RGB(40, 40, 45));
    }
}

static void draw_song_list(void)
{
    // Draw outer rounded border box (232x204 at X=4, Y=28) with light cyan border and black fill
    draw_rounded_box(4, 28, 232, 204, 12, RG_COLOR_BLACK, RG_COLOR_RGB(80, 180, 240), 2);
    rg_gui_set_font_size(8);
    for (int i = 0; i < VISIBLE_ITEMS; i++) {
        draw_list_item(i);
    }
}

static void draw_mini_player(void)
{
    // Clear bottom area below list to remove old progress bars and residual artifacts
    rg_gui_draw_rect(0, 230, SCREEN_W, 90, MUSIC_BG);

    // Draw compact outer rounded purple rectangle at bottom
    draw_rounded_box(MINI_BOX_X, MINI_BOX_Y, MINI_BOX_W, MINI_BOX_H, 10, MINI_BOX_BG, MINI_BOX_BORDER, 2);

    const char *icon = is_playing ? "> " : "|| ";
    if (pipeline_has_run && current_track < total_tracks) {
        const char *raw_name = strlen(info_title_str) ? info_title_str : playlist[current_track];
        char clean_name[128];
        format_clean_song_name(raw_name, clean_name, sizeof(clean_name));
        int name_len = strlen(clean_name);
        int ofs = 0;
        if (name_len > MINI_MAX_NAME_CHARS) {
            ofs = mini_scroll_char_offset;
            int max_ofs = name_len - MINI_MAX_NAME_CHARS;
            if (ofs > max_ofs) ofs = max_ofs;
        }
        char display[80];
        snprintf(display, sizeof(display), "%s%s", icon, clean_name + ofs);
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(MINI_TRACK_X, MINI_TRACK_Y, MINI_TRACK_W, MINI_TRACK_H, MINI_BOX_BG, RG_COLOR_WHITE, display, 8);
    } else {
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(MINI_TRACK_X, MINI_TRACK_Y, MINI_TRACK_W, MINI_TRACK_H, MINI_BOX_BG, RG_COLOR_WHITE, "|| No track playing", 8);
    }
}

static void draw_music_header(void)
{
    rg_gui_draw_rect(0, 0, SCREEN_W, SEP1_Y, MUSIC_BG);
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_box(0, TITLE_Y, SCREEN_W, TITLE_H, MUSIC_BG, "MUSIC");
    if (is_shuffle) {
        rg_gui_set_font_size(16);
        rg_gui_draw_text(216, 4, "S", RG_COLOR_RGB(180, 100, 255), MUSIC_BG);
    }
    rg_gui_draw_rect(0, SEP1_Y, SCREEN_W, 1, SEP_COLOR);
}

static void draw_music_full_ui(void)
{
    rg_gui_clear(MUSIC_BG);
    rg_display_drain();

    draw_music_header();

    if (total_tracks > 0) {
        draw_song_list();
    } else {
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(0, LIST_Y + 2 * LIST_ITEM_H, SCREEN_W, LIST_ITEM_H,
                              MUSIC_BG, RG_COLOR_RGB(150, 150, 150),
                              "  No MP3 files found", 4);
    }

    draw_mini_player();

    list_full_dirty = false;
    scroll_only_dirty = false;
    player_dirty = false;
}

static void scroll_tick(void)
{
    if (total_tracks == 0) return;
    char clean_name[128];
    format_clean_song_name(playlist[selected_index], clean_name, sizeof(clean_name));
    int name_len = strlen(clean_name);
    if (name_len <= MAX_NAME_CHARS) {
        scroll_char_offset = 0;
        return;
    }
    int64_t now = esp_timer_get_time() / 1000;
    if (now - scroll_timer >= SCROLL_INTERVAL_MS) {
        scroll_timer = now;
        scroll_char_offset++;
        if (scroll_char_offset > name_len - MAX_NAME_CHARS + 3) {
            scroll_char_offset = 0;
        }
        scroll_only_dirty = true;
    }
}

// ---- Player UI Drawing Functions ----
static void draw_player_title(void)
{
    if (current_track >= total_tracks) return;
    const char *raw_name = strlen(info_title_str) ? info_title_str : playlist[current_track];
    char clean_name[128];
    format_clean_song_name(raw_name, clean_name, sizeof(clean_name));
    int name_len = strlen(clean_name);
    int max_chars = 14;
    int ofs = 0;
    int left_pad = 0;
    if (name_len > max_chars) {
        ofs = player_scroll_char_offset;
        int max_ofs = name_len - max_chars;
        if (ofs > max_ofs) ofs = max_ofs;
        left_pad = 8;
    } else {
        left_pad = (SCREEN_W - (name_len * 16)) / 2;
        if (left_pad < 0) left_pad = 0;
    }
    char display[32];
    snprintf(display, sizeof(display), "%.*s", max_chars, clean_name + ofs);
    
    rg_gui_set_font_size(16);
    rg_gui_draw_text_line(0, 16, SCREEN_W, 24, MUSIC_BG, RG_COLOR_WHITE, display, left_pad);
}

static int artist_scroll = 0;
static int64_t artist_scroll_timer = 0;

static void draw_player_metadata(void)
{
    char fmt_str[64];
    snprintf(fmt_str, sizeof(fmt_str), "%s | %d.%01d kHz | %d kbps",
             info_format_str,
             info_sample_rate / 1000,
             (info_sample_rate % 1000) / 100,
             info_bitrate);

    const char *artist = info_artist_str;
    int len = strlen(artist);
    if (len > 30) {
        int max = len - 30;
        if (artist_scroll > max)
            artist_scroll = 0;
        artist += artist_scroll;
    }

    char display[80];
    snprintf(display, sizeof(display), " %.*s", 30, artist);

    rg_gui_draw_rect(0, 50, SCREEN_W, 16, MUSIC_BG);
    rg_gui_set_font_size(8);
    rg_gui_draw_text_line(0, 50, SCREEN_W, 16, MUSIC_BG, RG_COLOR_RGB(200, 200, 210), display, 8);

    rg_gui_draw_rect(0, 72, SCREEN_W, 16, MUSIC_BG);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_center(SCREEN_W / 2, 72, fmt_str);
}

static void draw_player_timer(void)
{
    if (!fatfs_stream_reader || current_track_bytes == 0) return;
    float prog = get_playback_progress();
    int tot_sec = (info_bitrate > 0) ? (int)((current_track_bytes * 8ULL) / (info_bitrate * 1000ULL)) : 0;
    int cur_sec = (int)(prog * tot_sec);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d / %02d:%02d",
             cur_sec / 60, cur_sec % 60, tot_sec / 60, tot_sec % 60);

    rg_gui_draw_rect(0, 90, SCREEN_W, 20, MUSIC_BG);
    rg_gui_set_font_size(8);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_center(SCREEN_W / 2, 96, time_str);
}

static void draw_player_top_area(bool full_redraw)
{
    if (full_redraw) {
        rg_gui_draw_rect(0, 0, SCREEN_W, 123, MUSIC_BG);
        rg_gui_draw_rect(0, 124, SCREEN_W, 1, SEP_COLOR);
    }
    rg_gui_draw_rect(0, 0, SCREEN_W, 14, MUSIC_BG);
    rg_gui_set_font_size(8);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_set_fill_color(MUSIC_BG);
    rg_gui_draw_text_center(SCREEN_W / 2, 4, "NOW PLAYING");
    if (is_shuffle) {
        rg_gui_set_font_size(8);
        rg_gui_draw_text(220, 3, "S", RG_COLOR_RGB(180, 100, 255), MUSIC_BG);
        rg_gui_set_text_color(RG_COLOR_WHITE);
    }
    draw_player_title();
    draw_player_metadata();
    draw_player_timer();
}

static void draw_volume_bar(void)
{
    rg_gui_draw_rect(220, 138, 14, 164, RG_COLOR_WHITE);
    int interior_h = 160;
    int fill_h = (current_volume * interior_h) / 100;
    if (fill_h < 0) fill_h = 0;
    if (fill_h > interior_h) fill_h = interior_h;
    int empty_h = interior_h - fill_h;

    if (empty_h > 0) {
        rg_gui_draw_rect(222, 140, 10, empty_h, RG_COLOR_RGB(100, 100, 110));
    }
    if (fill_h > 0) {
        rg_gui_draw_rect(222, 140 + empty_h, 10, fill_h, RG_COLOR_RGB(0, 230, 180));
    }
}

static void compute_fft_128(float *real, float *imag)
{
    int n = 128;
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * 3.14159265358979323846f / len;
        float wlen_r = cosf(angle);
        float wlen_i = sinf(angle);
        for (int i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            int half = len >> 1;
            for (int k = 0; k < half; k++) {
                int idx1 = i + k;
                int idx2 = i + k + half;
                float u_r = real[idx1];
                float u_i = imag[idx1];
                float v_r = real[idx2] * w_r - imag[idx2] * w_i;
                float v_i = real[idx2] * w_i + imag[idx2] * w_r;
                real[idx1] = u_r + v_r;
                imag[idx1] = u_i + v_i;
                real[idx2] = u_r - v_r;
                imag[idx2] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }
}

typedef struct {
    const uint8_t *jpg_data;
    uint32_t jpg_size;
    uint32_t jpg_offset;
    uint16_t *out_img;
    int out_w;
    int out_h;
} tjpg_session_t;

static UINT tjpg_in_func(JDEC *jd, BYTE *buff, UINT nbyte)
{
    tjpg_session_t *sess = (tjpg_session_t *)jd->device;
    if (sess->jpg_offset >= sess->jpg_size) return 0;
    UINT rem = sess->jpg_size - sess->jpg_offset;
    if (nbyte > rem) nbyte = rem;
    if (buff) {
        memcpy(buff, sess->jpg_data + sess->jpg_offset, nbyte);
    }
    sess->jpg_offset += nbyte;
    return nbyte;
}

static UINT tjpg_out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    tjpg_session_t *sess = (tjpg_session_t *)jd->device;
    if (!sess->out_img) return 0;
    uint8_t *rgb = (uint8_t *)bitmap;
    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;
    for (int y = 0; y < h; y++) {
        int dst_y = rect->top + y;
        if (dst_y >= sess->out_h) continue;
        for (int x = 0; x < w; x++) {
            int dst_x = rect->left + x;
            if (dst_x >= sess->out_w) continue;
            uint8_t r = rgb[(y * w + x) * 3 + 0];
            uint8_t g = rgb[(y * w + x) * 3 + 1];
            uint8_t b = rgb[(y * w + x) * 3 + 2];
            uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            uint16_t color_sw = (uint16_t)((color >> 8) | (color << 8));
            sess->out_img[dst_y * sess->out_w + dst_x] = color_sw;
        }
    }
    return 1;
}

static void send_vis_buf_to_display(void)
{
    if (!vis_buf) return;
    static uint16_t dma_line_buf[180 * 10] __attribute__((aligned(4)));
    int box_s = 180;
    int lines_per_chunk = 10;
    for (int y = 0; y < box_s; y += lines_per_chunk) {
        int lines = (y + lines_per_chunk <= box_s) ? lines_per_chunk : (box_s - y);
        memcpy(dma_line_buf, &vis_buf[y * box_s], lines * box_s * sizeof(uint16_t));
        rg_display_write(30, 134 + y, box_s, lines, box_s * 2, dma_line_buf);
        rg_display_drain();
    }
}

static void draw_player_thumbnail(void)
{
    ESP_LOGI(TAG, "draw_player_thumbnail called: show=%d, track=%d, apic_off=%lu, apic_len=%lu",
             show_thumbnail, current_track, (unsigned long)current_apic_offset, (unsigned long)current_apic_size);

    if (!vis_buf) {
        vis_buf = heap_caps_malloc(180 * 180 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!vis_buf) vis_buf = malloc(180 * 180 * sizeof(uint16_t));
    }
    if (!vis_buf) return;

    int box_s = 180;
    uint16_t bg_sw = (uint16_t)((MUSIC_BG >> 8) | (MUSIC_BG << 8));
    for (int i = 0; i < box_s * box_s; i++) vis_buf[i] = bg_sw;

    bool drawn_ok = false;
    if (current_apic_size > 0 && current_apic_offset > 0 && total_tracks > 0 && playlist && playlist[current_track]) {
        char path[512];
        snprintf(path, sizeof(path), "/sdcard/%s", playlist[current_track]);
        FILE *f = fopen(path, "rb");
        if (f) {
            if (fseek(f, current_apic_offset, SEEK_SET) == 0) {
                uint8_t *jpg_data = heap_caps_malloc(current_apic_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                if (!jpg_data) jpg_data = heap_caps_malloc(current_apic_size, MALLOC_CAP_8BIT);
                if (!jpg_data) jpg_data = malloc(current_apic_size);
                if (jpg_data) {
                    size_t read_bytes = fread(jpg_data, 1, current_apic_size, f);
                    if (read_bytes == current_apic_size) {
                        tjpg_session_t sess;
                        sess.jpg_data = jpg_data;
                        sess.jpg_size = current_apic_size;
                        sess.jpg_offset = 0;
                        sess.out_img = NULL;
                        sess.out_w = 0;
                        sess.out_h = 0;

                        JDEC jd;
                        char *pool = heap_caps_malloc(4096, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
                        if (!pool) pool = malloc(4096);
                        if (pool) {
                            JRESULT res_prep = jd_prepare(&jd, tjpg_in_func, pool, 4096, &sess);
                            if (res_prep == JDR_OK) {
                                uint8_t scale = 0;
                                while ((jd.width >> (scale + 1)) >= 180 && (jd.height >> (scale + 1)) >= 180 && scale < 3) {
                                    scale++;
                                }
                                int dec_w = jd.width >> scale;
                                int dec_h = jd.height >> scale;
                                sess.out_w = dec_w;
                                sess.out_h = dec_h;
                                sess.out_img = heap_caps_malloc(dec_w * dec_h * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
                                if (!sess.out_img) sess.out_img = heap_caps_malloc(dec_w * dec_h * sizeof(uint16_t), MALLOC_CAP_8BIT);
                                if (!sess.out_img) sess.out_img = malloc(dec_w * dec_h * sizeof(uint16_t));

                                if (sess.out_img) {
                                    JRESULT res_dec = jd_decomp(&jd, tjpg_out_func, scale);
                                    if (res_dec == JDR_OK) {
                                        float scale_x = 180.0f / (float)dec_w;
                                        float scale_y = 180.0f / (float)dec_h;
                                        float s = (scale_x < scale_y) ? scale_x : scale_y;
                                        int draw_w = (int)(dec_w * s);
                                        int draw_h = (int)(dec_h * s);
                                        if (draw_w > 180) draw_w = 180;
                                        if (draw_h > 180) draw_h = 180;
                                        int offset_x = (180 - draw_w) / 2;
                                        int offset_y = (180 - draw_h) / 2;

                                        for (int ty = 0; ty < draw_h; ty++) {
                                            for (int tx = 0; tx < draw_w; tx++) {
                                                int sx = (tx * dec_w) / draw_w;
                                                int sy = (ty * dec_h) / draw_h;
                                                if (sx >= dec_w) sx = dec_w - 1;
                                                if (sy >= dec_h) sy = dec_h - 1;
                                                vis_buf[(offset_y + ty) * 180 + (offset_x + tx)] = sess.out_img[sy * dec_w + sx];
                                            }
                                        }
                                        drawn_ok = true;
                                        ESP_LOGI(TAG, "Thumbnail decompressed OK (%lu x %lu -> %d x %d)", (unsigned long)jd.width, (unsigned long)jd.height, draw_w, draw_h);
                                    } else {
                                        ESP_LOGW(TAG, "jd_decomp failed: %d", (int)res_dec);
                                    }
                                    free(sess.out_img);
                                } else {
                                    ESP_LOGW(TAG, "Failed to allocate out_img (%d x %d)", dec_w, dec_h);
                                }
                            } else {
                                ESP_LOGW(TAG, "jd_prepare failed: %d (off=%lu, size=%lu)", (int)res_prep, (unsigned long)current_apic_offset, (unsigned long)current_apic_size);
                            }
                            free(pool);
                        } else {
                            ESP_LOGW(TAG, "Failed to allocate 4096 byte pool for TJpgDec");
                        }
                    } else {
                        ESP_LOGW(TAG, "fread failed: read %lu of %lu bytes", (unsigned long)read_bytes, (unsigned long)current_apic_size);
                    }
                    free(jpg_data);
                } else {
                    ESP_LOGW(TAG, "Failed to allocate %lu bytes for jpg_data", (unsigned long)current_apic_size);
                }
            } else {
                ESP_LOGW(TAG, "fseek to %lu failed", (unsigned long)current_apic_offset);
            }
            fclose(f);
        } else {
            ESP_LOGW(TAG, "fopen failed for %s", path);
        }
    } else {
        ESP_LOGW(TAG, "No valid APIC data for thumbnail: off=%lu, size=%lu, track=%d", (unsigned long)current_apic_offset, (unsigned long)current_apic_size, current_track);
    }

    send_vis_buf_to_display();

    if (!drawn_ok) {
        rg_gui_set_font_size(8);
        rg_gui_draw_text_box(30, 214, 180, 20, MUSIC_BG, "NO THUMBNAIL");
        rg_display_drain();
    }
}

static void draw_player_visualizer(void)
{
    if (show_thumbnail || !vis_buf) return;
    init_fft_state();

    int box_s = 180;
    int c = 90;
    uint16_t bg_sw = (uint16_t)((MUSIC_BG >> 8) | (MUSIC_BG << 8));
    for (int i = 0; i < box_s * box_s; i++) vis_buf[i] = bg_sw;

    int64_t now_us = esp_timer_get_time();
    float t_sec = (float)(now_us / 1000) * 0.005f;

    static float vu_meter_val = 0.0f;
    static float vu_meter_vel = 0.0f;
    static uint8_t pcm_read_buf[4096];
    static float fft_real[128];
    static float fft_imag[128];
    float target_vu = 0.0f;
    bool have_real_audio = false;

    if (is_playing && fft_ringbuf) {
        static int log_cnt = 0;
        int avail = rb_bytes_filled(fft_ringbuf);
        if (avail > 0) {
            if (avail > (int)sizeof(pcm_read_buf)) avail = (int)sizeof(pcm_read_buf);
            int bytes_read = rb_read(fft_ringbuf, (char*)pcm_read_buf, avail, 0);
            if (++log_cnt % 40 == 1) {
                ESP_LOGI(TAG,
                         "FFT avail=%d read=%d channels=%d",
                         avail,
                         bytes_read,
                         info_channels);
            }
            int bytes_per_sample = (info_channels == 2) ? 4 : 2;
            int total_samples = bytes_read / bytes_per_sample;
            if (total_samples >= 128) {
                int start_sample = total_samples - 128;
                int16_t *pcm16 = (int16_t*)pcm_read_buf;
                float sum_sq = 0.0f;
                for (int i = 0; i < 128; i++) {
                    int idx = (start_sample + i) * (info_channels == 2 ? 2 : 1);
                    float sample_val;
                    if (info_channels == 2) {
                        sample_val = 0.5f * ((float)pcm16[idx] + (float)pcm16[idx + 1]);
                    } else {
                        sample_val = (float)pcm16[idx];
                    }
                    float norm_s = sample_val / 32768.0f;
                    sum_sq += norm_s * norm_s;
                    float hann = 0.5f * (1.0f - cosf(6.2831853f * (float)i / 127.0f));
                    fft_real[i] = norm_s * hann;
                    fft_imag[i] = 0.0f;
                }
                float rms = sqrtf(sum_sq / 128.0f);
                target_vu = rms * 3.5f;
                if (target_vu > 1.0f) target_vu = 1.0f;
                compute_fft_128(fft_real, fft_imag);
                have_real_audio = true;
            }
        }
    }

    if (have_real_audio) {
        if (target_vu > vu_meter_val) {
            vu_meter_val = target_vu;
            vu_meter_vel = 0.04f;
        } else {
            vu_meter_vel -= 0.008f;
            vu_meter_val += vu_meter_vel;
            if (vu_meter_val < 0.0f) { vu_meter_val = 0.0f; vu_meter_vel = 0.0f; }
        }
    } else {
        vu_meter_val *= 0.85f;
    }
    int r_start = 30 + (int)(vu_meter_val * 8.0f);

    for (int i = 0; i < NUM_FFT_BANDS; i++) {
        if (have_real_audio) {
            int k_start = (int)(1.0f + powf((float)i / 48.0f, 1.4f) * 60.0f);
            int k_end   = (int)(1.0f + powf((float)(i + 1) / 48.0f, 1.4f) * 60.0f);
            if (k_end <= k_start) k_end = k_start + 1;
            if (k_end > 63) k_end = 63;
            float band_max = 0.0f;
            for (int k = k_start; k < k_end; k++) {
                float mag = sqrtf(fft_real[k] * fft_real[k] + fft_imag[k] * fft_imag[k]);
                if (mag > band_max) band_max = mag;
            }
            float freq_boost = 1.0f + ((float)i / (float)NUM_FFT_BANDS) * 2.5f;
            float target = band_max * 18.0f * freq_boost;
            if (target > 48.0f) target = 48.0f;
            if (target < 2.0f) target = 2.0f;

            if (target > fft_val[i]) {
                fft_val[i] = target;
                fft_vel[i] = 2.0f;
            } else {
                fft_vel[i] -= 1.5f;
                fft_val[i] += fft_vel[i];
                if (fft_val[i] < 2.0f) {
                    fft_val[i] = 2.0f;
                    fft_vel[i] = 0.0f;
                }
            }
        } else {
            fft_vel[i] -= 1.5f;
            fft_val[i] += fft_vel[i];
            if (fft_val[i] < 2.0f) {
                fft_val[i] = 2.0f;
                fft_vel[i] = 0.0f;
            }
        }

        float theta = i * (2.0f * 3.14159265f / (float)NUM_FFT_BANDS) - 1.5707963f;
        float cos_t = cosf(theta);
        float sin_t = sinf(theta);
        int r_end = r_start + (int)fft_val[i];
        if (r_end > 86) r_end = 86;

        int x0 = c + (int)(r_start * cos_t);
        int y0 = c + (int)(r_start * sin_t);
        int x1 = c + (int)(r_end * cos_t);
        int y1 = c + (int)(r_end * sin_t);

        uint16_t col = get_rainbow_color(i, NUM_FFT_BANDS);
        draw_line_in_buf(vis_buf, box_s, box_s, x0, y0, x1, y1, col);
        int ox = (int)(-sin_t * 1.0f);
        int oy = (int)(cos_t * 1.0f);
        draw_line_in_buf(vis_buf, box_s, box_s, x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
        draw_line_in_buf(vis_buf, box_s, box_s, x0 - ox, y0 - oy, x1 - ox, y1 - oy, col);
    }

    float pulse = sinf(t_sec) * 0.5f + 0.5f;
    int core_r = 10 + (int)(9.0f * pulse);
    if (core_r >= r_start - 4) core_r = r_start - 5;
    if (core_r < 4) core_r = 4;
    for (int y = c - core_r; y <= c + core_r; y++) {
        for (int x = c - core_r; x <= c + core_r; x++) {
            int dx = x - c, dy = y - c;
            int r2 = dx * dx + dy * dy;
            if (r2 <= core_r * core_r) {
                int dist = (int)sqrtf((float)r2);
                int r_col = (int)(20 + 40 * pulse * (1.0f - (float)dist / core_r));
                int g_col = (int)(10 + 30 * pulse * (1.0f - (float)dist / core_r));
                int b_col = (int)(50 + 100 * pulse * (1.0f - (float)dist / core_r));
                uint16_t col = RG_COLOR_RGB(r_col, g_col, b_col);
                vis_buf[y * box_s + x] = (uint16_t)((col >> 8) | (col << 8));
            }
        }
    }

    float prog = get_playback_progress();
    int prog_inner = r_start - 3;
    int prog_outer = r_start;
    for (int y = c - prog_outer; y <= c + prog_outer; y++) {
        for (int x = c - prog_outer; x <= c + prog_outer; x++) {
            int dx = x - c, dy = y - c;
            int r2 = dx * dx + dy * dy;
            if (r2 >= prog_inner * prog_inner && r2 <= prog_outer * prog_outer) {
                float ang = atan2f((float)dy, (float)dx) + 1.5707963f;
                if (ang < 0.0f) ang += 6.2831853f;
                float norm_ang = ang / 6.2831853f;
                if (norm_ang <= prog) {
                    uint16_t col = RG_COLOR_RGB(0, 230, 180);
                    vis_buf[y * box_s + x] = (uint16_t)((col >> 8) | (col << 8));
                } else {
                    uint16_t col = RG_COLOR_RGB(40, 40, 50);
                    vis_buf[y * box_s + x] = (uint16_t)((col >> 8) | (col << 8));
                }
            }
        }
    }

    uint16_t icon_sw = (uint16_t)((RG_COLOR_WHITE >> 8) | (RG_COLOR_WHITE << 8));
    uint16_t pause_sw = (uint16_t)((PAUSED_CLR >> 8) | (PAUSED_CLR << 8));
    if (is_playing) {
        for (int py = -7; py <= 7; py++) {
            int max_px = 6 - (abs(py) * 11) / 7;
            for (int px = -5; px <= max_px; px++) {
                vis_buf[(c + py) * box_s + (c + px)] = icon_sw;
            }
        }
    } else {
        for (int py = -6; py <= 6; py++) {
            for (int px = -5; px <= -2; px++) {
                vis_buf[(c + py) * box_s + (c + px)] = pause_sw;
            }
            for (int px = 2; px <= 5; px++) {
                vis_buf[(c + py) * box_s + (c + px)] = pause_sw;
            }
        }
    }

    send_vis_buf_to_display();
}

static void draw_player_ui_full(void)
{
    if (!vis_buf) {
        vis_buf = heap_caps_malloc(180 * 180 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!vis_buf) vis_buf = malloc(180 * 180 * sizeof(uint16_t));
    }
    rg_gui_clear(MUSIC_BG);
    rg_display_drain();
    player_ui_top_dirty = true;
    draw_player_top_area(true);
    if (show_thumbnail) {
        draw_player_thumbnail();
    } else {
        draw_player_visualizer();
    }
    if (vol_bar_visible) {
        draw_volume_bar();
    }
}

// ---- Play Track ----
static void play_track(int index)
{
    if (index < 0 || index >= total_tracks) return;

    if (pipeline_has_run) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_terminate(pipeline);
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_pipeline_reset_elements(pipeline);
    }
    if (fft_ringbuf) {
        rb_reset(fft_ringbuf);
    }

    char path[256];
    snprintf(path, sizeof(path), "/sdcard/%s", playlist[index]);

    // Get file size for progress bar
    struct stat st;
    current_track_bytes = (stat(path, &st) == 0) ? st.st_size : 0;

    audio_element_set_uri(fatfs_stream_reader, path);
    ESP_LOGI(TAG, "Playing Track [%d/%d]: %s", index + 1, total_tracks, playlist[index]);

    const char *ext = strrchr(playlist[index], '.');
    if (ext) {
        if (strcasecmp(ext, ".flac") == 0) strcpy(info_format_str, "FLAC");
        else if (strcasecmp(ext, ".wav") == 0) strcpy(info_format_str, "WAV");
        else if (strcasecmp(ext, ".aac") == 0 || strcasecmp(ext, ".m4a") == 0) strcpy(info_format_str, "AAC");
        else strcpy(info_format_str, "MP3");
    } else {
        strcpy(info_format_str, "MP3");
    }

    strcpy(info_artist_str, "Unknown Artist");
    info_title_str[0] = '\0';
    const char *dash = strstr(playlist[index], " - ");
    if (dash && (dash - playlist[index] < sizeof(info_artist_str))) {
        int len = (int)(dash - playlist[index]);
        strncpy(info_artist_str, playlist[index], len);
        info_artist_str[len] = '\0';
        const char *t_start = dash + 3;
        int t_len = strlen(t_start);
        const char *dot = strrchr(t_start, '.');
        if (dot) t_len = (int)(dot - t_start);
        int copy_len = t_len < sizeof(info_title_str) - 1 ? t_len : sizeof(info_title_str) - 1;
        strncpy(info_title_str, t_start, copy_len);
        info_title_str[copy_len] = '\0';
    }

    // Check for embedded thumbnail (ID3v2 APIC) and Artist tag (TPE1 / Vorbis)
    {
        current_apic_offset = 0;
        current_apic_size = 0;
        FILE *f = fopen(path, "rb");
        if (f) {
            uint8_t hdr[10];
            bool has_id3 = false;
            bool has_apic = false;
            uint32_t id3_size = 0;
            if (fread(hdr, 1, 10, f) == 10 &&
                hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
                has_id3 = true;
                id3_size = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                           ((uint32_t)(hdr[7] & 0x7F) << 14) |
                           ((uint32_t)(hdr[8] & 0x7F) << 7)  |
                           ((uint32_t)(hdr[9] & 0x7F));
                
                uint32_t pos = 10;
                if (hdr[5] & 0x40) { // Extended header
                    uint8_t ext_hdr[4];
                    if (fread(ext_hdr, 1, 4, f) == 4) {
                        uint32_t ext_size = ((uint32_t)ext_hdr[0] << 24) | ((uint32_t)ext_hdr[1] << 16) | ((uint32_t)ext_hdr[2] << 8) | ext_hdr[3];
                        if (hdr[3] == 4) {
                            ext_size = ((uint32_t)(ext_hdr[0] & 0x7F) << 21) | ((uint32_t)(ext_hdr[1] & 0x7F) << 14) | ((uint32_t)(ext_hdr[2] & 0x7F) << 7) | (uint32_t)(ext_hdr[3] & 0x7F);
                        }
                        pos += ext_size;
                    }
                }

                while (pos + 10 <= 10 + id3_size) {
                    if (fseek(f, pos, SEEK_SET) != 0) break;
                    if (fread(hdr, 1, 10, f) != 10) break;
                    char id[5] = { (char)hdr[0], (char)hdr[1], (char)hdr[2], (char)hdr[3], '\0' };
                    if (id[0] == 0 || !isalnum((unsigned char)id[0])) break;
                    
                    uint32_t f_size = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 8) | hdr[7];
                    if (hdr[3] == 4) {
                        f_size = ((uint32_t)(hdr[4] & 0x7F) << 21) | ((uint32_t)(hdr[5] & 0x7F) << 14) | ((uint32_t)(hdr[6] & 0x7F) << 7) | (uint32_t)(hdr[7] & 0x7F);
                    }
                    if (f_size == 0 || pos + 10 + f_size > 10 + id3_size) break;

                    if (strcmp(id, "APIC") == 0) {
                        has_apic = true;
                        static uint8_t apic_hdr[4096];
                        uint32_t read_len = f_size < sizeof(apic_hdr) ? f_size : sizeof(apic_hdr);
                        if (fread(apic_hdr, 1, read_len, f) == read_len) {
                            for (uint32_t k = 0; k + 2 <= read_len; k++) {
                                if (apic_hdr[k] == 0xFF && apic_hdr[k+1] == 0xD8) {
                                    current_apic_offset = pos + 10 + k;
                                    current_apic_size = f_size - k;
                                    break;
                                } else if (k + 4 <= read_len && apic_hdr[k] == 0x89 && apic_hdr[k+1] == 0x50 && apic_hdr[k+2] == 0x4E && apic_hdr[k+3] == 0x47) {
                                    current_apic_offset = pos + 10 + k;
                                    current_apic_size = f_size - k;
                                    break;
                                }
                            }
                            if (current_apic_offset == 0) {
                                ESP_LOGW(TAG, "APIC tag found (%lu bytes), but JPEG/PNG magic not found in first %lu bytes!", (unsigned long)f_size, (unsigned long)read_len);
                            }
                        } else {
                            ESP_LOGW(TAG, "Failed to read %lu bytes of APIC tag", (unsigned long)read_len);
                        }
                    } else if ((strcmp(id, "TPE1") == 0 || strcmp(id, "TIT2") == 0) && f_size > 1 && f_size < 1024) {
                        uint8_t tag_buf[256];
                        uint32_t r_len = f_size < sizeof(tag_buf) ? f_size : sizeof(tag_buf);
                        if (fread(tag_buf, 1, r_len, f) == r_len) {
                            if (f_size > r_len) fseek(f, f_size - r_len, SEEK_CUR);
                            uint8_t enc = tag_buf[0];
                            char *dest = (strcmp(id, "TPE1") == 0) ? info_artist_str : info_title_str;
                            int max_len = (strcmp(id, "TPE1") == 0) ? 60 : 120;
                            int out_idx = 0;
                            if (enc == 0 || enc == 3) {
                                for (uint32_t k = 1; k < r_len && out_idx < max_len; k++) {
                                    char c = (char)tag_buf[k];
                                    if (c == '\0') break;
                                    if ((unsigned char)c >= 32) dest[out_idx++] = c;
                                }
                            } else if (enc == 1 || enc == 2) {
                                uint32_t start_k = (enc == 1 && r_len >= 3) ? 3 : 1;
                                for (uint32_t k = start_k; k + 1 < r_len && out_idx < max_len; k += 2) {
                                    char c = (char)tag_buf[enc == 2 ? k + 1 : k];
                                    if (c == '\0' && tag_buf[k + 1] == '\0') break;
                                    if ((unsigned char)c >= 32 && tag_buf[enc == 2 ? k : k + 1] == 0) {
                                        dest[out_idx++] = c;
                                    }
                                }
                            }
                            if (out_idx > 0) dest[out_idx] = '\0';
                        }
                    }
                    pos += 10 + f_size;
                }
            }

            if (!has_id3 || strcmp(info_artist_str, "Unknown Artist") == 0 || info_title_str[0] == '\0') {
                fseek(f, 0, SEEK_SET);
                uint32_t fb_len = 8192;
                uint8_t *fb_buf = malloc(fb_len);
                if (fb_buf) {
                    size_t got = fread(fb_buf, 1, fb_len, f);
                    if (strcmp(info_artist_str, "Unknown Artist") == 0) {
                        for (size_t i = 0; i + 7 < got; i++) {
                            if (strncasecmp((const char *)&fb_buf[i], "artist=", 7) == 0) {
                                int out_idx = 0;
                                for (size_t k = i + 7; k < got && out_idx < 60; k++) {
                                    char c = (char)fb_buf[k];
                                    if (c < 32 || c == 0 || c == 0xFF) break;
                                    info_artist_str[out_idx++] = c;
                                }
                                if (out_idx > 0) info_artist_str[out_idx] = '\0';
                                break;
                            }
                        }
                    }
                    if (info_title_str[0] == '\0') {
                        for (size_t i = 0; i + 6 < got; i++) {
                            if (strncasecmp((const char *)&fb_buf[i], "title=", 6) == 0) {
                                int out_idx = 0;
                                for (size_t k = i + 6; k < got && out_idx < 120; k++) {
                                    char c = (char)fb_buf[k];
                                    if (c < 32 || c == 0 || c == 0xFF) break;
                                    info_title_str[out_idx++] = c;
                                }
                                if (out_idx > 0) info_title_str[out_idx] = '\0';
                                break;
                            }
                        }
                    }
                    free(fb_buf);
                }
            }
            fclose(f);
            ESP_LOGI(TAG, "Thumbnail: ID3=%s, APIC=%s (off=%lu, size=%lu), ID3size=%lu, Artist='%s', Title='%s'",
                     has_id3 ? "YES" : "NO",
                     has_apic ? "YES" : "NO",
                     (unsigned long)current_apic_offset,
                     (unsigned long)current_apic_size,
                     (unsigned long)id3_size,
                     info_artist_str,
                     info_title_str);
        }
    }

    audio_pipeline_run(pipeline);
    pipeline_has_run = true;
    is_playing = true;
    current_track = index;

    list_full_dirty = true;
    player_dirty = true;
    mini_scroll_char_offset = 0;
    mini_scroll_timer = esp_timer_get_time() / 1000;
    artist_scroll = 0;
    artist_scroll_timer = esp_timer_get_time() / 1000;
}

// ---- Public API ----
void app_music_init(audio_hal_handle_t hal_handle)
{
    music_hal_handle = hal_handle;
}

void app_music_start(void)
{
    ESP_LOGI(TAG, "Music App Started");
    in_player_ui = false;
    if (!music_initialized) {
        if (!playlist_mutex) {
            playlist_mutex = xSemaphoreCreateMutex();
        }
        if (!playlist) {
            playlist = calloc(MAX_PLAYLIST_FILES, sizeof(char *));
        }
        xTaskCreate(sd_card_scan_task, "sd_scan_task", 4096, NULL, 5, NULL);
        while (scan_in_progress && total_tracks < 100) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        last_known_total_tracks = total_tracks;
        music_initialized = true;
    }
    selected_index = current_track;
    ensure_cursor_visible();
    scroll_char_offset = 0;
    scroll_timer = esp_timer_get_time() / 1000;
    draw_music_full_ui();
    rg_display_drain();
}

void app_music_stop(void)
{
    ESP_LOGI(TAG, "Music App Stopped");
    in_player_ui = false;
    if (vis_buf) {
        free(vis_buf);
        vis_buf = NULL;
    }
    if (pipeline && pipeline_has_run) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_terminate(pipeline);
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_pipeline_reset_elements(pipeline);
        is_playing = false;
        pipeline_has_run = false;
    }
    if (fft_ringbuf) {
        rb_reset(fft_ringbuf);
    }
}

static void pause_current_track(void)
{
    if (!is_playing) return;
    audio_element_info_t info = {0};
    audio_element_getinfo(fatfs_stream_reader, &info);
    saved_byte_pos = info.byte_pos;
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    is_playing = false;
    player_dirty = true;
    ESP_LOGI(TAG, "Paused track at byte_pos = %lld", (long long)saved_byte_pos);
}

static void resume_current_track(void)
{
    if (is_playing || !pipeline_has_run) return;
    if (fft_ringbuf) rb_reset(fft_ringbuf);
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);
    audio_pipeline_reset_ringbuffer(pipeline);
    audio_pipeline_reset_elements(pipeline);

    char path[256];
    snprintf(path, sizeof(path), "/sdcard/%s", playlist[current_track]);
    audio_element_set_uri(fatfs_stream_reader, path);
    audio_element_set_byte_pos(fatfs_stream_reader, (int)saved_byte_pos);

    audio_pipeline_run(pipeline);
    is_playing = true;
    player_dirty = true;
    ESP_LOGI(TAG, "Resumed track from byte_pos = %lld", (long long)saved_byte_pos);
}

static int get_next_track_idx(void)
{
    if (total_tracks <= 0) return 0;
    if (is_shuffle && total_tracks > 1) {
        int next = rand() % total_tracks;
        while (next == current_track) next = rand() % total_tracks;
        return next;
    }
    return (current_track + 1) % total_tracks;
}

static int get_prev_track_idx(void)
{
    if (total_tracks <= 0) return 0;
    if (is_shuffle && total_tracks > 1) {
        int prev = rand() % total_tracks;
        while (prev == current_track) prev = rand() % total_tracks;
        return prev;
    }
    return (current_track - 1 + total_tracks) % total_tracks;
}

void app_music_handle_input(button_event_t event)
{
    switch (event) {
        case BTN_UP:
            if (in_player_ui) {
                current_volume = (current_volume + 10 > 100) ? 100 : current_volume + 10;
                uint8_t reg_val = (uint8_t)(((100 - current_volume) * 192) / 100);
                es8388_write_reg(ES8388_DACCONTROL4, reg_val);
                es8388_write_reg(ES8388_DACCONTROL5, reg_val);
                vol_bar_visible = true;
                vol_bar_timer = esp_timer_get_time() / 1000;
                draw_volume_bar();
            } else if (total_tracks > 0) {
                prev_selected = selected_index;
                selected_index = (selected_index - 1 + total_tracks) % total_tracks; // Button up moves list down
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
                for (int i = 0; i < VISIBLE_ITEMS; i++) {
                    slot_dirty[i] = true;
                }
            }
            break;
        case BTN_DOWN:
            if (in_player_ui) {
                current_volume = (current_volume - 10 < 0) ? 0 : current_volume - 10;
                uint8_t reg_val = (uint8_t)(((100 - current_volume) * 192) / 100);
                es8388_write_reg(ES8388_DACCONTROL4, reg_val);
                es8388_write_reg(ES8388_DACCONTROL5, reg_val);
                vol_bar_visible = true;
                vol_bar_timer = esp_timer_get_time() / 1000;
                draw_volume_bar();
            } else if (total_tracks > 0) {
                prev_selected = selected_index;
                selected_index = (selected_index + 1) % total_tracks; // Button down moves list up
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
                for (int i = 0; i < VISIBLE_ITEMS; i++) {
                    slot_dirty[i] = true;
                }
            }
            break;
        case BTN_LEFT:
            if (total_tracks > 0) {
                int prev = get_prev_track_idx();
                play_track(prev);
                selected_index = current_track;
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
                if (in_player_ui) {
                    player_scroll_char_offset = 0;
                    player_scroll_timer = esp_timer_get_time() / 1000;
                    draw_player_ui_full();
                }
            }
            break;
        case BTN_RIGHT:
            if (total_tracks > 0) {
                int next = get_next_track_idx();
                play_track(next);
                selected_index = current_track;
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
                if (in_player_ui) {
                    player_scroll_char_offset = 0;
                    player_scroll_timer = esp_timer_get_time() / 1000;
                    draw_player_ui_full();
                }
            }
            break;
        case BTN_ENTER:
            if (total_tracks > 0) {
                if (in_player_ui) {
                    if (is_playing) {
                        pause_current_track();
                    } else if (pipeline_has_run) {
                        resume_current_track();
                    }
                    if (show_thumbnail) {
                        draw_player_thumbnail();
                    } else {
                        draw_player_visualizer();
                    }
                } else {
                    if (selected_index == current_track && is_playing) {
                        // Already playing, just open player ui
                    } else if (selected_index == current_track && !is_playing && pipeline_has_run) {
                        resume_current_track();
                    } else {
                        play_track(selected_index);
                    }
                    in_player_ui = true;
                    player_scroll_char_offset = 0;
                    player_scroll_timer = esp_timer_get_time() / 1000;
                    draw_player_ui_full();
                }
            }
            break;
        case BTN_A:
            if (total_tracks > 0) {
                show_thumbnail = !show_thumbnail;
                if (in_player_ui) {
                    if (show_thumbnail) {
                        draw_player_thumbnail();
                    } else {
                        if (fft_ringbuf) rb_reset(fft_ringbuf);
                        draw_player_visualizer();
                    }
                } else {
                    if (selected_index == current_track && is_playing) {
                        // Already playing, just open player ui
                    } else if (selected_index == current_track && !is_playing && pipeline_has_run) {
                        resume_current_track();
                    } else {
                        play_track(selected_index);
                    }
                    in_player_ui = true;
                    player_scroll_char_offset = 0;
                    player_scroll_timer = esp_timer_get_time() / 1000;
                    draw_player_ui_full();
                }
            }
            break;
        case BTN_ESCAPE:
            if (in_player_ui) {
                in_player_ui = false;
                selected_index = current_track;
                ensure_cursor_visible();
                list_full_dirty = true;
                player_dirty = true;
                draw_music_full_ui();
            }
            break;
        case BTN_B:
            is_shuffle = !is_shuffle;
            ESP_LOGI(TAG, "Shuffle mode: %s", is_shuffle ? "ON" : "OFF");
            if (in_player_ui) {
                draw_player_top_area(true);
            } else {
                list_full_dirty = true;
                draw_music_full_ui();
            }
            break;
        case BTN_VOL_UP:
            {
                current_volume = (current_volume + 10 > 100) ? 100 : current_volume + 10;
                uint8_t reg_val = (uint8_t)(((100 - current_volume) * 192) / 100);
                es8388_write_reg(ES8388_DACCONTROL4, reg_val);
                es8388_write_reg(ES8388_DACCONTROL5, reg_val);
                ESP_LOGI(TAG, "Volume: %d (reg=0x%02x)", current_volume, reg_val);
                if (in_player_ui) {
                    vol_bar_visible = true;
                    vol_bar_timer = esp_timer_get_time() / 1000;
                    draw_volume_bar();
                }
            }
            break;
        case BTN_VOL_DOWN:
            {
                current_volume = (current_volume - 10 < 0) ? 0 : current_volume - 10;
                uint8_t reg_val = (uint8_t)(((100 - current_volume) * 192) / 100);
                es8388_write_reg(ES8388_DACCONTROL4, reg_val);
                es8388_write_reg(ES8388_DACCONTROL5, reg_val);
                ESP_LOGI(TAG, "Volume: %d (reg=0x%02x)", current_volume, reg_val);
                if (in_player_ui) {
                    vol_bar_visible = true;
                    vol_bar_timer = esp_timer_get_time() / 1000;
                    draw_volume_bar();
                }
            }
            break;
        default:
            break;
    }
}

void app_music_tick(void)
{
    if (total_tracks != last_known_total_tracks) {
        last_known_total_tracks = total_tracks;
        list_full_dirty = true;
    }
    // 1. Handle audio pipeline events
    if (evt) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, 0) == ESP_OK) {
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
                && msg.source == (void *)mp3_decoder
                && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(mp3_decoder, &music_info);
                i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates,
                                   music_info.bits, music_info.channels);
                info_sample_rate = music_info.sample_rates;
                info_bits = music_info.bits;
                info_channels = music_info.channels;
                if (music_info.bps > 0) {
                    info_bitrate = music_info.bps / 1000;
                } else if (strcmp(info_format_str, "FLAC") == 0) {
                    info_bitrate = 850;
                } else if (strcmp(info_format_str, "WAV") == 0) {
                    info_bitrate = (info_sample_rate * info_bits * info_channels) / 1000;
                } else {
                    info_bitrate = 320;
                }
                if (in_player_ui) {
                    draw_player_metadata();
                    draw_player_timer();
                }
            }
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
                && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
                && ((int)msg.data == AEL_STATUS_STATE_FINISHED || (int)msg.data == AEL_STATUS_STATE_STOPPED)) {
                if (is_playing && (msg.source == (void *)fatfs_stream_reader || msg.source == (void *)mp3_decoder || msg.source == (void *)i2s_stream_writer)) {
                    static int64_t last_auto_advance = 0;
                    int64_t now_ms = esp_timer_get_time() / 1000;
                    if (now_ms - last_auto_advance >= 1500) {
                        last_auto_advance = now_ms;
                        ESP_LOGI(TAG, "Element finished/stopped (source=%p, status=%d), auto-advancing to next track", msg.source, (int)msg.data);
                        current_track = get_next_track_idx();
                        play_track(current_track);
                        selected_index = current_track;
                        ensure_cursor_visible();
                        scroll_char_offset = 0;
                        if (in_player_ui) {
                            player_scroll_char_offset = 0;
                            player_scroll_timer = now_ms;
                            draw_player_ui_full();
                        }
                    }
                }
            }
        }
    }

    // If in Player UI, handle visualizer, time updates, title scrolling, volume bar
    if (in_player_ui) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - artist_scroll_timer > 200) {
            artist_scroll_timer = now_ms;
            if (strlen(info_artist_str) > 30) {
                artist_scroll++;
                draw_player_metadata();
            }
        }
        if (!show_thumbnail && (now_ms - last_vis_time >= 25)) {
            last_vis_time = now_ms;
            draw_player_visualizer();
        }
        if (is_playing && now_ms - last_time_update >= 500) {
            last_time_update = now_ms;
            draw_player_timer();
        }
        if (current_track < total_tracks) {
            const char *raw_name = strlen(info_title_str) ? info_title_str : playlist[current_track];
            char clean_name[128];
            format_clean_song_name(raw_name, clean_name, sizeof(clean_name));
            int name_len = strlen(clean_name);
            if (name_len > 14) {
                if (now_ms - player_scroll_timer >= 150) {
                    player_scroll_timer = now_ms;
                    player_scroll_char_offset++;
                    int max_ofs = name_len - 14;
                    if (player_scroll_char_offset > max_ofs + 4) {
                        player_scroll_char_offset = 0;
                    }
                    draw_player_title();
                }
            }
        }
        if (vol_bar_visible && (now_ms - vol_bar_timer >= 3000)) {
            vol_bar_visible = false;
            rg_gui_draw_rect(220, 138, 14, 164, MUSIC_BG);
        }
        return;
    }

    // 2. Text scrolling for selected item
    scroll_tick();

    // 2b. Mini player track name scrolling
    if (pipeline_has_run && current_track < total_tracks) {
        const char *mname = strlen(info_title_str) ? info_title_str : playlist[current_track];
        char clean_name[128];
        format_clean_song_name(mname, clean_name, sizeof(clean_name));
        int mlen = strlen(clean_name);
        if (mlen > MINI_MAX_NAME_CHARS) {
            int64_t mnow = esp_timer_get_time() / 1000;
            if (mnow - mini_scroll_timer >= MINI_SCROLL_INTERVAL_MS) {
                mini_scroll_timer = mnow;
                mini_scroll_char_offset++;
                int mmax = mlen - MINI_MAX_NAME_CHARS;
                if (mini_scroll_char_offset > mmax + 4) {
                    mini_scroll_char_offset = 0;
                }
                mini_player_scroll_dirty = true;
            }
        } else {
            mini_scroll_char_offset = 0;
        }
    }

    // 3. Redraw list if dirty
    if (list_full_dirty) {
        draw_music_header();
        draw_song_list();
        list_full_dirty = false;
        scroll_only_dirty = false;
        for (int i = 0; i < VISIBLE_ITEMS; i++) slot_dirty[i] = false;
    } else {
        // Partial slot redraws
        for (int i = 0; i < VISIBLE_ITEMS; i++) {
            if (slot_dirty[i]) {
                rg_gui_set_font_size(8);
                draw_list_item(i);
                slot_dirty[i] = false;
            }
        }
        if (scroll_only_dirty) {
            rg_gui_set_font_size(8);
            int sel_slot = selected_index - view_start;
            if (sel_slot >= 0 && sel_slot < VISIBLE_ITEMS) {
                draw_list_item(sel_slot);
            }
            scroll_only_dirty = false;
        }
    }

    // 4. Redraw mini player if dirty
    if (player_dirty) {
        draw_mini_player();
        player_dirty = false;
        mini_player_scroll_dirty = false;
    } else if (mini_player_scroll_dirty) {
        // Only redraw the track name line for scroll updates
        if (pipeline_has_run && current_track < total_tracks) {
            const char *sname = strlen(info_title_str) ? info_title_str : playlist[current_track];
            char clean_name[128];
            format_clean_song_name(sname, clean_name, sizeof(clean_name));
            int slen = strlen(clean_name);
            int ofs = mini_scroll_char_offset;
            int max_ofs = slen - MINI_MAX_NAME_CHARS;
            if (max_ofs < 0) max_ofs = 0;
            if (ofs > max_ofs) ofs = max_ofs;
            char display[80];
            const char *icon = is_playing ? "> " : "|| ";
            snprintf(display, sizeof(display), "%s%s", icon, clean_name + ofs);
            rg_gui_set_font_size(8);
            rg_gui_draw_text_line(MINI_TRACK_X, MINI_TRACK_Y, MINI_TRACK_W, MINI_TRACK_H,
                                  MINI_BOX_BG, RG_COLOR_WHITE, display, 8);
        }
        mini_player_scroll_dirty = false;
    }
}

bool app_music_is_in_player_ui(void)
{
    return in_player_ui;
}
