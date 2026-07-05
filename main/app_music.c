#include "app_music.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "audio_event_iface.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "es8388.h"
#include "audio_volume.h"
#include <stdio.h>

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

#define MINI_TRACK_Y     262
#define MINI_TRACK_H     12

#define MINI_PROGRESS_Y  280
#define MINI_PROGRESS_H  4
#define PROGRESS_BG_CLR  RG_COLOR_RGB(40, 40, 40)
#define PROGRESS_FG_CLR  RG_COLOR_RGB(30, 200, 80)

#define MINI_STATUS_Y    290
#define MINI_STATUS_H    12

// ---- UI State ----
static int selected_index = 0;
static int view_start = 0;

static int scroll_char_offset = 0;
static int64_t scroll_timer = 0;
#define SCROLL_INTERVAL_MS 150
#define MAX_NAME_CHARS     27

static size_t current_track_bytes = 0;
static int64_t progress_timer = 0;

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

static void init_audio_pipeline(void);

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
        size_t len = strlen(ent->d_name);
        if (len > 4 && strcasecmp(ent->d_name + len - 4, ".mp3") == 0) {
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
    ESP_LOGI(TAG, "Background scan finished. Total MP3 tracks found: %d", total_tracks);
    scan_in_progress = false;
    vTaskDelete(NULL);
}

// ---- Audio Pipeline Init ----
static void init_audio_pipeline(void)
{
    ESP_LOGI(TAG, "Initializing Audio Pipeline...");
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

    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    const char *link_tag[3] = {"file", "mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);
}

// ---- Navigation ----
static void ensure_cursor_visible(void)
{
    if (total_tracks <= VISIBLE_ITEMS) {
        view_start = 0;
        return;
    }
    if (selected_index < view_start) {
        view_start = selected_index;
    } else if (selected_index >= view_start + VISIBLE_ITEMS) {
        view_start = selected_index - VISIBLE_ITEMS + 1;
    }
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
static void draw_progress_bar(void)
{
    float progress = get_playback_progress();
    int fill_w = (int)(progress * SCREEN_W);
    if (fill_w < 0) fill_w = 0;
    if (fill_w > SCREEN_W) fill_w = SCREEN_W;

    static uint16_t prog_buf[240 * 4] __attribute__((aligned(4)));
    uint16_t bg_sw = (uint16_t)((PROGRESS_BG_CLR >> 8) | (PROGRESS_BG_CLR << 8));
    uint16_t fg_sw = (uint16_t)((PROGRESS_FG_CLR >> 8) | (PROGRESS_FG_CLR << 8));

    for (int y = 0; y < MINI_PROGRESS_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            prog_buf[y * SCREEN_W + x] = (x < fill_w) ? fg_sw : bg_sw;
        }
    }
    rg_display_write(0, MINI_PROGRESS_Y, SCREEN_W, MINI_PROGRESS_H, SCREEN_W * 2, prog_buf);
    rg_display_drain();
}

static void draw_list_item(int slot)
{
    int track_idx = view_start + slot;
    int y = LIST_Y + slot * LIST_ITEM_H;

    if (track_idx >= total_tracks) {
        rg_gui_draw_rect(0, y, SCREEN_W, LIST_ITEM_H, MUSIC_BG);
        return;
    }

    bool is_sel = (track_idx == selected_index);
    bool is_cur_play = (track_idx == current_track && is_playing);
    bool is_cur_pause = (track_idx == current_track && !is_playing && pipeline_has_run);

    uint16_t bg = is_sel ? HIGHLIGHT_BG : MUSIC_BG;
    uint16_t fg = is_cur_play ? PLAYING_CLR : (is_cur_pause ? PAUSED_CLR : RG_COLOR_WHITE);

    const char *prefix = (is_cur_play || is_cur_pause) ? "> " : "  ";
    const char *name = playlist[track_idx];
    int name_len = strlen(name);

    int ofs = is_sel ? scroll_char_offset : 0;
    int max_ofs = name_len - MAX_NAME_CHARS;
    if (max_ofs < 0) max_ofs = 0;
    if (ofs > max_ofs) ofs = max_ofs;

    char display[128];
    snprintf(display, sizeof(display), "%s%s", prefix, name + ofs);

    rg_gui_set_font_size(8);
    rg_gui_draw_text_line(0, y, SCREEN_W, LIST_ITEM_H, bg, fg, display, 4);
}

static void draw_song_list(void)
{
    rg_gui_set_font_size(8);
    for (int i = 0; i < VISIBLE_ITEMS; i++) {
        draw_list_item(i);
    }
}

static void draw_mini_player(void)
{
    // Track name with scrolling
    if (pipeline_has_run && current_track < total_tracks) {
        const char *name = playlist[current_track];
        int name_len = strlen(name);
        int ofs = 0;
        if (name_len > MINI_MAX_NAME_CHARS) {
            ofs = mini_scroll_char_offset;
            int max_ofs = name_len - MINI_MAX_NAME_CHARS;
            if (ofs > max_ofs) ofs = max_ofs;
        }
        char display[64];
        const char *icon = is_playing ? "> " : "= ";
        snprintf(display, sizeof(display), "%s%s", icon, name + ofs);
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(0, MINI_TRACK_Y, SCREEN_W, MINI_TRACK_H,
                              MUSIC_BG, RG_COLOR_WHITE, display, 8);
    } else {
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(0, MINI_TRACK_Y, SCREEN_W, MINI_TRACK_H,
                              MUSIC_BG, RG_COLOR_RGB(100, 100, 100),
                              "  No track playing", 8);
    }

    // Progress bar
    draw_progress_bar();

    // Status text
    if (pipeline_has_run) {
        const char *status = is_playing ? "PLAYING" : "PAUSED";
        uint16_t sc = is_playing ? PLAYING_CLR : PAUSED_CLR;
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(0, MINI_STATUS_Y, SCREEN_W, MINI_STATUS_H,
                              MUSIC_BG, sc, status, 8);
    } else {
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(0, MINI_STATUS_Y, SCREEN_W, MINI_STATUS_H,
                              MUSIC_BG, SEP_COLOR, "STOPPED", 8);
    }
}

static void draw_music_full_ui(void)
{
    rg_gui_clear(MUSIC_BG);
    rg_display_drain();

    // Title
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_box(0, TITLE_Y, SCREEN_W, TITLE_H, MUSIC_BG, "MUSIC PLAYER");

    // Separator below title
    rg_gui_draw_rect(0, SEP1_Y, SCREEN_W, 1, SEP_COLOR);

    // Song list
    if (total_tracks > 0) {
        draw_song_list();
    } else {
        rg_gui_set_font_size(8);
        rg_gui_draw_text_line(0, LIST_Y + 2 * LIST_ITEM_H, SCREEN_W, LIST_ITEM_H,
                              MUSIC_BG, RG_COLOR_RGB(150, 150, 150),
                              "  No MP3 files found", 4);
    }

    // Separator above mini player
    rg_gui_draw_rect(0, SEP2_Y, SCREEN_W, 1, SEP_COLOR);

    // Mini player
    draw_mini_player();

    list_full_dirty = false;
    scroll_only_dirty = false;
    player_dirty = false;
}

static void scroll_tick(void)
{
    if (total_tracks == 0) return;
    const char *name = playlist[selected_index];
    int name_len = strlen(name);
    if (name_len <= MAX_NAME_CHARS) {
        scroll_char_offset = 0;
        return;
    }
    int64_t now = esp_timer_get_time() / 1000;
    if (now - scroll_timer >= SCROLL_INTERVAL_MS) {
        scroll_timer = now;
        scroll_char_offset++;
        int max_ofs = name_len - MAX_NAME_CHARS;
        if (scroll_char_offset > max_ofs + 4) {
            scroll_char_offset = 0;
        }
        scroll_only_dirty = true;
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

    char path[256];
    snprintf(path, sizeof(path), "/sdcard/%s", playlist[index]);

    // Get file size for progress bar
    struct stat st;
    current_track_bytes = (stat(path, &st) == 0) ? st.st_size : 0;

    audio_element_set_uri(fatfs_stream_reader, path);
    ESP_LOGI(TAG, "Playing Track [%d/%d]: %s", index + 1, total_tracks, playlist[index]);

    // Check for embedded thumbnail (ID3v2 APIC frame) and log to serial
    {
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
                // Scan for APIC frame (album art) in first 4KB
                uint32_t scan_len = id3_size < 4096 ? id3_size : 4096;
                uint8_t *buf = malloc(scan_len);
                if (buf) {
                    fseek(f, 10, SEEK_SET);
                    size_t got = fread(buf, 1, scan_len, f);
                    for (size_t i = 0; i + 3 < got; i++) {
                        if (buf[i]=='A' && buf[i+1]=='P' && buf[i+2]=='I' && buf[i+3]=='C') {
                            has_apic = true;
                            break;
                        }
                    }
                    free(buf);
                }
            }
            fclose(f);
            ESP_LOGI(TAG, "Thumbnail: ID3=%s, APIC=%s, ID3size=%lu",
                     has_id3 ? "YES" : "NO",
                     has_apic ? "YES" : "NO",
                     (unsigned long)id3_size);
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
}

// ---- Public API ----
void app_music_init(audio_hal_handle_t hal_handle)
{
    music_hal_handle = hal_handle;
}

void app_music_start(void)
{
    ESP_LOGI(TAG, "Music App Started");
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
    if (pipeline && pipeline_has_run) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_terminate(pipeline);
        audio_pipeline_reset_ringbuffer(pipeline);
        audio_pipeline_reset_elements(pipeline);
        is_playing = false;
        pipeline_has_run = false;
    }
}

void app_music_handle_input(button_event_t event)
{
    switch (event) {
        case BTN_UP:
            if (total_tracks > 0) {
                int old_vs = view_start;
                prev_selected = selected_index;
                selected_index = (selected_index - 1 + total_tracks) % total_tracks;
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
                if (view_start != old_vs) {
                    list_full_dirty = true;
                } else {
                    int os = prev_selected - view_start;
                    int ns = selected_index - view_start;
                    if (os >= 0 && os < VISIBLE_ITEMS) slot_dirty[os] = true;
                    if (ns >= 0 && ns < VISIBLE_ITEMS) slot_dirty[ns] = true;
                }
            }
            break;
        case BTN_DOWN:
            if (total_tracks > 0) {
                int old_vs2 = view_start;
                prev_selected = selected_index;
                selected_index = (selected_index + 1) % total_tracks;
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
                if (view_start != old_vs2) {
                    list_full_dirty = true;
                } else {
                    int os2 = prev_selected - view_start;
                    int ns2 = selected_index - view_start;
                    if (os2 >= 0 && os2 < VISIBLE_ITEMS) slot_dirty[os2] = true;
                    if (ns2 >= 0 && ns2 < VISIBLE_ITEMS) slot_dirty[ns2] = true;
                }
            }
            break;
        case BTN_LEFT:
            if (total_tracks > 0) {
                int prev = (current_track - 1 + total_tracks) % total_tracks;
                play_track(prev);
                selected_index = current_track;
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
            }
            break;
        case BTN_RIGHT:
            if (total_tracks > 0) {
                int next = (current_track + 1) % total_tracks;
                play_track(next);
                selected_index = current_track;
                ensure_cursor_visible();
                scroll_char_offset = 0;
                scroll_timer = esp_timer_get_time() / 1000;
            }
            break;
        case BTN_ENTER:
        case BTN_A:
            if (total_tracks > 0) {
                if (selected_index == current_track && is_playing) {
                    audio_pipeline_pause(pipeline);
                    is_playing = false;
                    list_full_dirty = true;
                    player_dirty = true;
                } else if (selected_index == current_track && !is_playing && pipeline_has_run) {
                    audio_pipeline_resume(pipeline);
                    is_playing = true;
                    list_full_dirty = true;
                    player_dirty = true;
                } else {
                    play_track(selected_index);
                }
            }
            break;
        case BTN_VOL_UP:
            {
                current_volume = (current_volume + 5 > 100) ? 100 : current_volume + 5;
                // Bypass audio_hal_set_volume to avoid legacy/new I2C driver conflict (NACK bug).
                // Write ES8388 DAC volume registers directly.
                // ES8388 volume range: 0x00 = 0dB, 0xC0 = -96dB. Map 0-100 linearly.
                uint8_t reg_val = (uint8_t)(((100 - current_volume) * 192) / 100);
                es8388_write_reg(ES8388_DACCONTROL4, reg_val);
                es8388_write_reg(ES8388_DACCONTROL5, reg_val);
                ESP_LOGI(TAG, "Volume: %d (reg=0x%02x)", current_volume, reg_val);
            }
            break;
        case BTN_VOL_DOWN:
            {
                current_volume = (current_volume - 5 < 0) ? 0 : current_volume - 5;
                uint8_t reg_val = (uint8_t)(((100 - current_volume) * 192) / 100);
                es8388_write_reg(ES8388_DACCONTROL4, reg_val);
                es8388_write_reg(ES8388_DACCONTROL5, reg_val);
                ESP_LOGI(TAG, "Volume: %d (reg=0x%02x)", current_volume, reg_val);
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
            }
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
                && msg.source == (void *)i2s_stream_writer
                && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
                && (int)msg.data == AEL_STATUS_STATE_FINISHED) {
                current_track = (current_track + 1) % total_tracks;
                play_track(current_track);
                selected_index = current_track;
                ensure_cursor_visible();
                scroll_char_offset = 0;
            }
        }
    }

    // 2. Text scrolling for selected item
    scroll_tick();

    // 2b. Mini player track name scrolling
    if (pipeline_has_run && current_track < total_tracks) {
        const char *mname = playlist[current_track];
        int mlen = strlen(mname);
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
            const char *sname = playlist[current_track];
            int slen = strlen(sname);
            int ofs = mini_scroll_char_offset;
            int max_ofs = slen - MINI_MAX_NAME_CHARS;
            if (max_ofs < 0) max_ofs = 0;
            if (ofs > max_ofs) ofs = max_ofs;
            char display[64];
            const char *icon = is_playing ? "> " : "= ";
            snprintf(display, sizeof(display), "%s%s", icon, sname + ofs);
            rg_gui_set_font_size(8);
            rg_gui_draw_text_line(0, MINI_TRACK_Y, SCREEN_W, MINI_TRACK_H,
                                  MUSIC_BG, RG_COLOR_WHITE, display, 8);
        }
        mini_player_scroll_dirty = false;
    }

    // 5. Update progress bar periodically when playing
    if (is_playing) {
        int64_t now = esp_timer_get_time() / 1000;
        if (now - progress_timer >= 500) {
            draw_progress_bar();
            progress_timer = now;
        }
    }
}
