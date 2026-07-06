#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "app_files.h"

static const char *TAG = "APP_FILES";

// Layout Constants
#define SCREEN_W         240
#define SCREEN_H         320
#define FILES_BG         RG_COLOR_BLACK

#define TITLE_Y          2
#define TITLE_H          20
#define SEP1_Y           24

#define LIST_Y           28
#define FLIST_H          204
#define VISIBLE_ITEMS    6
#define LIST_ITEM_H      33

#define MAX_FILES        500
#define MAX_NAME_CHARS   26

// App Modes: 0 = File List, 1 = Page View, 2 = RSVP Mode
static int app_mode = 0;
static bool ui_dirty = true;

// File List State
static char (*file_list)[128] = NULL;
static int total_files = 0;
static int selected_index = 0;
static int view_start = 0;

// Page & RSVP State
static FILE *current_file = NULL;
static long file_size = 0;
static long page_start_pos = 0;
static char active_filename[128] = {0};

// RSVP Engine State
static char current_word[64];
static char pending_word[64] = {0};
static int rsvp_wpm = 250;
static bool rsvp_paused = false;
static int64_t last_word_time = 0;
static int rsvp_delay_ms = 240;

#define HISTORY_SIZE 150
static long pos_history[HISTORY_SIZE] = {0};
static int history_idx = 0;
static int history_count = 0;

// Forward Declarations
static void draw_file_list_ui(void);
static void draw_page_view_ui(void);
static void draw_rsvp_ui(void);
static void load_page_view(void);
static void load_previous_page(void);
static bool get_next_word(void);
static void display_word(void);
static void save_bookmark(long position);
static long load_bookmark(void);
static void sanitize_smart_punctuation(char *str);

// ---- Rounded Box Helper ----
static void draw_rounded_box(int x, int y, int w, int h, int r, uint16_t fill_color, uint16_t border_color, int border_width)
{
    static uint16_t *box_buf = NULL;
    static int box_buf_cap = 0;
    int needed = w * h * (int)sizeof(uint16_t);
    if (needed > box_buf_cap) {
        if (box_buf) free(box_buf);
        box_buf = malloc(needed);
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
    rg_display_write(x, y, w, h, w * 2, box_buf);
    rg_display_drain();
}

// ---- Bookmark Logic ----
static void get_bookmark_path(char *out_path, size_t max_len)
{
    char base_name[128];
    strncpy(base_name, active_filename, sizeof(base_name) - 1);
    base_name[127] = '\0';
    char *ext = strrchr(base_name, '.');
    if (ext != NULL) strcpy(ext, ".BMK");
    else strcat(base_name, ".BMK");
    snprintf(out_path, max_len, "/sdcard/%s", base_name);
}

static void save_bookmark(long position)
{
    if (strlen(active_filename) == 0) return;
    char filepath[256];
    get_bookmark_path(filepath, sizeof(filepath));
    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "%ld", position);
        fflush(f);
        fclose(f);
    }
}

static long load_bookmark(void)
{
    if (strlen(active_filename) == 0) return 0;
    char filepath[256];
    get_bookmark_path(filepath, sizeof(filepath));
    FILE *f = fopen(filepath, "r");
    long pos = 0;
    if (f) {
        fscanf(f, "%ld", &pos);
        fclose(f);
    }
    return pos;
}

// ---- Text Parsing & Sanitization ----
static void sanitize_smart_punctuation(char *str)
{
    char *src = str;
    char *dst = str;
    while (*src) {
        unsigned char c = (unsigned char)*src;
        if (c == 0x91 || c == 0x92 || c == 0x60 || c == 0xB4) { *dst++ = '\''; src++; continue; }
        if (c == 0x93 || c == 0x94) { *dst++ = '"'; src++; continue; }
        if (c == 0x96 || c == 0x97) { *dst++ = '-'; src++; continue; }
        if (c == 0x85) { *dst++ = '.'; *dst++ = '.'; *dst++ = '.'; src++; continue; }
        if (c == 0xE2 && (unsigned char)src[1] == 0x80) {
            unsigned char c3 = (unsigned char)src[2];
            if (c3 == 0x98 || c3 == 0x99) { *dst++ = '\''; src += 3; continue; }
            if (c3 == 0x9C || c3 == 0x9D) { *dst++ = '"'; src += 3; continue; }
            if (c3 == 0x93 || c3 == 0x94) { *dst++ = '-'; src += 3; continue; }
            if (c3 == 0xA6) { *dst++ = '.'; *dst++ = '.'; *dst++ = '.'; src += 3; continue; }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

// ---- File List & SD Card Scanning ----
static void load_file_list(void)
{
    if (!file_list) {
        file_list = calloc(MAX_FILES, sizeof(file_list[0]));
    }
    total_files = 0;
    if (!file_list) return;

    DIR *dir = opendir("/sdcard");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sdcard directory");
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && total_files < MAX_FILES) {
        if (entry->d_name[0] == '.') continue;
        if (strstr(entry->d_name, ".txt") || strstr(entry->d_name, ".TXT")) {
            strncpy(file_list[total_files], entry->d_name, 127);
            file_list[total_files][127] = '\0';
            total_files++;
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Found %d text files on SD card", total_files);
}

static void ensure_cursor_visible(void)
{
    view_start = selected_index - 2;
}

static void draw_file_item(int slot)
{
    int idx = view_start + slot;
    int y = 31 + slot * LIST_ITEM_H;
    bool is_sel = (slot == 2);

    if (idx < 0 || idx >= total_files) {
        if (is_sel) {
            draw_rounded_box(6, y + 1, 228, 31, 6, RG_COLOR_RGB(140, 60, 220), RG_COLOR_RGB(140, 60, 220), 0);
            rg_gui_set_font_size(8);
            rg_gui_draw_text_line(12, y + 12, 216, 16, RG_COLOR_RGB(140, 60, 220), RG_COLOR_WHITE, total_files == 0 ? "  No TXT Files Found" : "  ---", 8);
        } else {
            rg_gui_draw_rect(6, y, 228, LIST_ITEM_H, FILES_BG);
            if (slot < 5 && slot != 1 && slot != 2) {
                rg_gui_draw_rect(14, y + 32, 212, 1, RG_COLOR_RGB(40, 40, 45));
            }
        }
        return;
    }

    uint16_t bg = is_sel ? RG_COLOR_RGB(140, 60, 220) : FILES_BG;
    uint16_t fg = RG_COLOR_WHITE;

    if (is_sel) {
        draw_rounded_box(6, y + 1, 228, 31, 6, bg, bg, 0);
    } else {
        rg_gui_draw_rect(6, y, 228, LIST_ITEM_H, FILES_BG);
    }

    rg_gui_set_font_size(8);
    rg_gui_draw_text_line(12, y + 12, 216, 16, bg, fg, file_list[idx], 4);

    if (slot < 5 && slot != 1 && slot != 2) {
        rg_gui_draw_rect(14, y + 32, 212, 1, RG_COLOR_RGB(40, 40, 45));
    }
}

static void draw_file_list_ui(void)
{
    rg_gui_clear(FILES_BG);
    rg_display_drain();

    // Top Header
    rg_gui_draw_rect(0, 0, SCREEN_W, SEP1_Y, FILES_BG);
    rg_gui_set_font_size(16);
    rg_gui_set_text_color(RG_COLOR_WHITE);
    rg_gui_draw_text_box(0, TITLE_Y, SCREEN_W, TITLE_H, FILES_BG, "TEXT FILES");
    rg_gui_draw_rect(0, SEP1_Y, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

    // Outer List Box
    draw_rounded_box(4, LIST_Y, 232, FLIST_H, 12, FILES_BG, RG_COLOR_RGB(160, 80, 240), 2);

    rg_gui_set_font_size(8);
    for (int i = 0; i < VISIBLE_ITEMS; i++) {
        draw_file_item(i);
    }

    // Bottom Box
    draw_rounded_box(4, 236, 232, 80, 10, FILES_BG, RG_COLOR_RGB(160, 80, 240), 2);
    rg_gui_set_font_size(8);
    char stat_str[64];
    snprintf(stat_str, sizeof(stat_str), "Total TXT Files: %d", total_files);
    rg_gui_draw_text_center(SCREEN_W / 2, 250, stat_str);
    rg_gui_draw_text_center(SCREEN_W / 2, 270, "UP/DOWN: Scroll List");
    rg_gui_draw_text_center(SCREEN_W / 2, 290, "ENTER: Open Page View");
}

// ---- Page View Logic & UI ----
static void open_text_file(const char *filename)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", filename);
    strncpy(active_filename, filename, sizeof(active_filename) - 1);
    active_filename[127] = '\0';

    if (current_file) fclose(current_file);
    current_file = fopen(filepath, "r");
    if (!current_file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return;
    }
    setvbuf(current_file, NULL, _IONBF, 0);
    fseek(current_file, 0, SEEK_END);
    file_size = ftell(current_file);

    long start_pos = load_bookmark();
    if (start_pos > 0 && start_pos < file_size) fseek(current_file, start_pos, SEEK_SET);
    else fseek(current_file, 0, SEEK_SET);

    app_mode = 1;
    load_page_view();
}

static void load_page_view(void)
{
    if (!current_file) return;

    int c;
    while ((c = fgetc(current_file)) != EOF && isspace(c));
    if (c != EOF) ungetc(c, current_file);

    page_start_pos = ftell(current_file);
    save_bookmark(page_start_pos);
    static char page_buf[420];
    memset(page_buf, 0, sizeof(page_buf));
    size_t bytes_read = fread(page_buf, 1, 400, current_file);

    if (bytes_read > 0) {
        char *last_space = strrchr(page_buf, ' ');
        char *last_nl = strrchr(page_buf, '\n');
        char *cut_point = (last_nl > last_space) ? last_nl : last_space;
        if (cut_point != NULL && cut_point != page_buf) {
            *cut_point = '\0';
            long rewind_amount = bytes_read - strlen(page_buf);
            fseek(current_file, -rewind_amount, SEEK_CUR);
        }
        sanitize_smart_punctuation(page_buf);
    } else {
        strcpy(page_buf, "End of Book.");
    }

    draw_page_view_ui();
    // Render text lines
    rg_gui_set_font_size(8);
    int y = 26;
    char word[64];
    char line_buf[64] = "";
    char *ptr = page_buf;
    while (*ptr && y < 290) {
        if (*ptr == '\n') {
            if (strlen(line_buf) > 0) {
                rg_gui_draw_text(6, y, line_buf, RG_COLOR_RGB(220, 220, 220), FILES_BG);
                line_buf[0] = '\0';
                y += 14;
            } else {
                y += 14;
            }
            ptr++;
            continue;
        }
        int i = 0;
        while (*ptr && *ptr != ' ' && *ptr != '\n' && i < 63) {
            word[i++] = *ptr++;
        }
        word[i] = '\0';
        if (*ptr == ' ') ptr++;

        if (strlen(line_buf) + strlen(word) + 1 > 28) {
            if (strlen(line_buf) > 0) {
                rg_gui_draw_text(6, y, line_buf, RG_COLOR_RGB(220, 220, 220), FILES_BG);
                y += 14;
                line_buf[0] = '\0';
            }
        }
        if (strlen(line_buf) > 0) strcat(line_buf, " ");
        strcat(line_buf, word);
    }
    if (strlen(line_buf) > 0 && y < 290) {
        rg_gui_draw_text(6, y, line_buf, RG_COLOR_RGB(220, 220, 220), FILES_BG);
    }
}

static void load_previous_page(void)
{
    if (!current_file) return;
    long back_jump = page_start_pos - 400;
    if (back_jump <= 0) {
        fseek(current_file, 0, SEEK_SET);
        load_page_view();
        return;
    }
    fseek(current_file, back_jump, SEEK_SET);
    int c;
    while ((c = fgetc(current_file)) != EOF && !isspace(c));
    load_page_view();
}

static void draw_page_view_ui(void)
{
    rg_gui_clear(FILES_BG);
    rg_display_drain();

    // Top Header
    rg_gui_draw_rect(0, 0, SCREEN_W, 20, FILES_BG);
    rg_gui_set_font_size(8);

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    rg_gui_draw_text(4, 5, time_str, RG_COLOR_RGB(150, 150, 150), FILES_BG);

    char name_trunc[20];
    strncpy(name_trunc, active_filename, 12);
    name_trunc[12] = '\0';
    rg_gui_draw_text_center(SCREEN_W / 2, 5, name_trunc);

    long pos = ftell(current_file);
    float prog = file_size > 0 ? ((float)pos * 100.0f / (float)file_size) : 0.0f;
    char prog_str[32];
    snprintf(prog_str, sizeof(prog_str), "%.2f%%", prog);
    rg_gui_draw_text(SCREEN_W - 60, 5, prog_str, RG_COLOR_RGB(150, 150, 150), FILES_BG);

    rg_gui_draw_rect(0, 20, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

    // Bottom Instructions
    rg_gui_draw_rect(0, 298, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));
    rg_gui_draw_text_center(SCREEN_W / 2, 305, "LEFT/RIGHT: Page | ENTER: RSVP");
}

// ---- RSVP Mode Logic & UI ----
static void start_rsvp_mode(void)
{
    app_mode = 2;
    fseek(current_file, page_start_pos, SEEK_SET);
    history_count = 0;
    history_idx = 0;
    pending_word[0] = '\0';
    rsvp_paused = false;
    last_word_time = esp_timer_get_time();
    get_next_word();
    draw_rsvp_ui();
}

static bool get_next_word(void)
{
    bool has_word = false;
    if (strlen(pending_word) > 0) {
        strcpy(current_word, pending_word);
        pending_word[0] = '\0';
        has_word = true;
    } else if (current_file) {
        long current_pos = ftell(current_file);
        if (fscanf(current_file, "%63s", current_word) == 1) {
            pos_history[history_idx] = current_pos;
            history_idx = (history_idx + 1) % HISTORY_SIZE;
            if (history_count < HISTORY_SIZE) history_count++;
            sanitize_smart_punctuation(current_word);
            has_word = true;
        }
    }
    if (has_word) {
        char *hyphen = strchr(current_word, '-');
        if (hyphen != NULL && hyphen != current_word && *(hyphen + 1) != '\0') {
            strcpy(pending_word, hyphen + 1);
            *(hyphen + 1) = '\0';
        }
        return true;
    }
    return false;
}

static void rewind_words(int num_words)
{
    if (!current_file || history_count <= 1) return;
    if (num_words >= history_count) num_words = history_count - 1;
    if (num_words == 0) return;
    int target_idx = (history_idx - num_words - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    long target_pos = pos_history[target_idx];
    fseek(current_file, target_pos, SEEK_SET);
    history_idx = target_idx;
    history_count -= num_words;
    pending_word[0] = '\0';
    get_next_word();
    display_word();
}

static void display_word(void)
{
    int len = strlen(current_word);
    if (len == 0) return;

    int orp_map[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5};
    int orp_idx = (len >= 13) ? 5 : orp_map[len];

    char left[64] = {0};
    char orp[4] = {0};
    char right[64] = {0};

    strncpy(left, current_word, orp_idx);
    orp[0] = current_word[orp_idx];
    strcpy(right, current_word + orp_idx + 1);

    // Clear word display area
    rg_gui_draw_rect(0, 120, SCREEN_W, 60, FILES_BG);

    // Increase ONLY the RSVP word font
    int word_font_size = 24;
    rg_gui_set_font_size(word_font_size);
    if (len * word_font_size > 232) {
        word_font_size = 16;
        rg_gui_set_font_size(word_font_size);
        if (len * word_font_size > 232) {
            word_font_size = 8;
            rg_gui_set_font_size(word_font_size);
        }
    }

    // Calculate actual rendered width (font advance)
    int orp_w = rg_gui_get_text_width(orp);
    int left_w = rg_gui_get_text_width(left);

    int center_x = SCREEN_W / 2;
    int orp_x = center_x - (orp_w / 2);
    int word_y = 150 - (word_font_size / 2);

    if (strlen(left) > 0) {
        rg_gui_draw_text_scaled(orp_x - left_w, word_y, left, RG_COLOR_WHITE, FILES_BG);
    }
    rg_gui_draw_text_scaled(orp_x, word_y, orp, RG_COLOR_RGB(255, 60, 60), FILES_BG);
    if (strlen(right) > 0) {
        rg_gui_draw_text_scaled(orp_x + orp_w, word_y, right, RG_COLOR_WHITE, FILES_BG);
    }

    // Reset font size back to 8 immediately so header/footer/WPM/progress are unchanged!
    rg_gui_set_font_size(8);

    int delay = 60000 / rsvp_wpm;
    char last_char = current_word[len - 1];
    if (last_char == '.' || last_char == '?' || last_char == '!') delay *= 2;
    else if (last_char == ',' || last_char == ';' || last_char == ':') delay = (int)(delay * 1.5);
    else if (last_char == '-') delay = (int)(delay * 1.2);
    rsvp_delay_ms = delay;

    // Update Progress up to 2 decimal points
    long pos = ftell(current_file);
    float prog = file_size > 0 ? ((float)pos * 100.0f / (float)file_size) : 0.0f;
    char prog_str[32];
    snprintf(prog_str, sizeof(prog_str), "%.2f%%", prog);
    rg_gui_draw_rect(SCREEN_W - 65, 2, 63, 16, FILES_BG);
    rg_gui_draw_text(SCREEN_W - 60, 5, prog_str, RG_COLOR_RGB(150, 150, 150), FILES_BG);
}

static void draw_rsvp_ui(void)
{
    rg_gui_clear(FILES_BG);
    rg_display_drain();

    // Header
    rg_gui_draw_rect(0, 0, SCREEN_W, 20, FILES_BG);
    rg_gui_set_font_size(8);
    char wpm_str[32];
    snprintf(wpm_str, sizeof(wpm_str), "RSVP | WPM: %d", rsvp_wpm);
    rg_gui_draw_text(6, 5, wpm_str, RG_COLOR_RGB(160, 80, 240), FILES_BG);
    rg_gui_draw_rect(0, 20, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));

    // Footer
    rg_gui_draw_rect(0, 280, SCREEN_W, 1, RG_COLOR_RGB(80, 80, 80));
    rg_gui_draw_text_center(SCREEN_W / 2, 290, "ENTER: Pause/Resume | UP/DOWN: WPM");
    rg_gui_draw_text_center(SCREEN_W / 2, 305, "LEFT/RIGHT: Rewind/Skip | ESC: Page View");

    display_word();
}

// ---- External Lifecycle Interfaces ----
void app_files_init(void)
{
    ESP_LOGI(TAG, "Initialized app_files");
}

void app_files_start(void)
{
    ESP_LOGI(TAG, "Starting app_files");
    load_file_list();
    selected_index = 0;
    ensure_cursor_visible();
    app_mode = 0;
    ui_dirty = true;
    draw_file_list_ui();
}

void app_files_stop(void)
{
    ESP_LOGI(TAG, "Stopping app_files");
    if (current_file) {
        if (app_mode == 1) {
            save_bookmark(page_start_pos);
        } else if (app_mode == 2) {
            save_bookmark(ftell(current_file));
        }
        fclose(current_file);
        current_file = NULL;
    }
    app_mode = 0;
}

bool app_files_is_in_page_view(void)
{
    return (app_mode == 1 || app_mode == 2);
}

void app_files_handle_input(button_event_t event)
{
    if (event == BTN_NONE) return;

    if (app_mode == 0) { // File List
        if (total_files == 0) return;
        if (event == BTN_UP || event == BTN_VOL_DOWN) {
            selected_index--;
            if (selected_index < 0) selected_index = total_files - 1;
            ensure_cursor_visible();
            draw_file_list_ui();
        } else if (event == BTN_DOWN || event == BTN_VOL_UP) {
            selected_index++;
            if (selected_index >= total_files) selected_index = 0;
            ensure_cursor_visible();
            draw_file_list_ui();
        } else if (event == BTN_ENTER || event == BTN_A) {
            open_text_file(file_list[selected_index]);
        }
    } else if (app_mode == 1) { // Page View
        if (event == BTN_RIGHT || event == BTN_DOWN || event == BTN_VOL_UP) {
            load_page_view();
        } else if (event == BTN_LEFT || event == BTN_UP || event == BTN_VOL_DOWN) {
            load_previous_page();
        } else if (event == BTN_ENTER || event == BTN_A) {
            start_rsvp_mode();
        } else if (event == BTN_ESCAPE || event == BTN_B) {
            if (current_file) {
                save_bookmark(page_start_pos);
                fclose(current_file);
                current_file = NULL;
            }
            app_mode = 0;
            draw_file_list_ui();
        }
    } else if (app_mode == 2) { // RSVP Mode
        if (event == BTN_ENTER || event == BTN_A) {
            rsvp_paused = !rsvp_paused;
            rg_gui_set_font_size(8);
            rg_gui_draw_rect(SCREEN_W / 2 - 30, 25, 60, 14, FILES_BG);
            if (rsvp_paused) rg_gui_draw_text_center(SCREEN_W / 2, 25, "[PAUSED]");
            last_word_time = esp_timer_get_time();
        } else if (event == BTN_UP || event == BTN_VOL_UP) {
            rsvp_wpm += 25;
            if (rsvp_wpm > 600) rsvp_wpm = 600;
            rg_gui_set_font_size(8);
            char wpm_str[32];
            snprintf(wpm_str, sizeof(wpm_str), "RSVP | WPM: %d", rsvp_wpm);
            rg_gui_draw_rect(0, 0, SCREEN_W / 2, 20, FILES_BG);
            rg_gui_draw_text(6, 5, wpm_str, RG_COLOR_RGB(160, 80, 240), FILES_BG);
        } else if (event == BTN_DOWN || event == BTN_VOL_DOWN) {
            rsvp_wpm -= 25;
            if (rsvp_wpm < 100) rsvp_wpm = 100;
            rg_gui_set_font_size(8);
            char wpm_str[32];
            snprintf(wpm_str, sizeof(wpm_str), "RSVP | WPM: %d", rsvp_wpm);
            rg_gui_draw_rect(0, 0, SCREEN_W / 2, 20, FILES_BG);
            rg_gui_draw_text(6, 5, wpm_str, RG_COLOR_RGB(160, 80, 240), FILES_BG);
        } else if (event == BTN_LEFT) {
            rewind_words(10);
        } else if (event == BTN_RIGHT) {
            if (rsvp_paused) {
                if (get_next_word()) display_word();
            } else {
                for (int i = 0; i < 10; i++) get_next_word();
                display_word();
            }
        } else if (event == BTN_ESCAPE || event == BTN_B) {
            if (current_file) save_bookmark(ftell(current_file));
            app_mode = 1;
            load_page_view();
        }
    }
}

void app_files_tick(void)
{
    if (app_mode == 2 && !rsvp_paused) {
        int64_t now = esp_timer_get_time();
        if ((now - last_word_time) >= (int64_t)rsvp_delay_ms * 1000ULL) {
            last_word_time = now;
            if (get_next_word()) {
                display_word();
            } else {
                rsvp_paused = true;
                rg_gui_set_font_size(16);
                rg_gui_draw_rect(0, 130, SCREEN_W, 40, FILES_BG);
                rg_gui_draw_text_center(SCREEN_W / 2, 140, "END OF BOOK");
                rg_gui_set_font_size(8);
                rg_gui_draw_text_center(SCREEN_W / 2, 170, "Press ESC to return");
            }
        }
    }
}
