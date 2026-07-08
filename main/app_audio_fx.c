#include "app_audio_fx.h"
#include "rg_gui.h"
#include "rg_display.h"
#include "esp_equalizer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

#define TAG "AUDIO_FX"
#define NVS_NAMESPACE "dsp_store"

extern int global_volume;

typedef struct {
    const char *name;
    int gains[10]; // 31, 62, 125, 250, 500, 1K, 2K, 4K, 8K, 16K
} eq_preset_t;

static const eq_preset_t eq_presets[] = {
    {"FLAT",      { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0}},
    {"ROCK",      { 5,  4,  3,  1, -1, -1,  0,  2,  4,  5}},
    {"POP",       {-1,  1,  4,  5,  4,  1, -1, -2, -1,  0}},
    {"JAZZ",      { 4,  3,  1,  2, -1, -1,  0,  1,  3,  4}},
    {"CLASSICAL", { 5,  4,  3,  2, -1, -1,  0,  2,  3,  5}},
    {"BASS",      { 8,  7,  5,  3,  1,  0,  0,  0,  0,  0}},
    {"VOCAL",     {-2, -1,  0,  2,  5,  5,  3,  1,  0, -1}},
};
#define NUM_PRESETS (sizeof(eq_presets) / sizeof(eq_presets[0]))

static void *eq_handle = NULL;
static bool dsp_enabled = true;
static int eq_gains[10] = {0};
static int current_sample_rate = 44100;
static uint8_t current_preset = 0;
static uint8_t speed_index = 1; // 0=0.75x, 1=1.0x, 2=1.25x, 3=1.5x
static const float speed_values[] = {0.75f, 1.0f, 1.25f, 1.5f};
#define NUM_SPEEDS 4

static int selected_band = 0;
static bool needs_redraw = false;

static void apply_eq_gains(void) {
    if (!eq_handle) return;
    for (int i = 0; i < 10; i++) {
        esp_equalizer_set_band_value(eq_handle, (float)eq_gains[i], i, 0);
        esp_equalizer_set_band_value(eq_handle, (float)eq_gains[i], i, 1);
    }
}

static void save_dsp_settings_to_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "eq_gains", eq_gains, sizeof(eq_gains));
        nvs_set_u8(h, "preset", current_preset);
        nvs_set_u8(h, "enabled", dsp_enabled ? 1 : 0);
        nvs_set_u8(h, "speed", speed_index);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_dsp_settings_from_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(eq_gains);
        if (nvs_get_blob(h, "eq_gains", eq_gains, &len) != ESP_OK) {
            memset(eq_gains, 0, sizeof(eq_gains));
        }
        if (nvs_get_u8(h, "preset", &current_preset) != ESP_OK) current_preset = 0;
        uint8_t en = 1;
        if (nvs_get_u8(h, "enabled", &en) == ESP_OK) {
            dsp_enabled = (en != 0);
        }
        if (nvs_get_u8(h, "speed", &speed_index) != ESP_OK) speed_index = 1;
        nvs_close(h);
    } else {
        memset(eq_gains, 0, sizeof(eq_gains));
        current_preset = 0;
        dsp_enabled = true;
        speed_index = 1;
    }
}

void app_audio_fx_init(void) {
    load_dsp_settings_from_nvs();
    eq_handle = esp_equalizer_init(2, current_sample_rate, 10, 0);
    if (eq_handle) {
        apply_eq_gains();
    } else {
        ESP_LOGE(TAG, "Failed to init equalizer");
    }
}

void app_audio_fx_start(void) {
    rg_display_drain();
    rg_gui_clear(0x0000);
    needs_redraw = true;
}

void app_audio_fx_stop(void) {
    save_dsp_settings_to_nvs();
}

static void check_custom_preset(void) {
    bool match = false;
    for (int p = 0; p < NUM_PRESETS; p++) {
        bool same = true;
        for (int i = 0; i < 10; i++) {
            if (eq_gains[i] != eq_presets[p].gains[i]) {
                same = false;
                break;
            }
        }
        if (same) {
            current_preset = p;
            match = true;
            break;
        }
    }
    if (!match) {
        current_preset = 255; // Custom
    }
}

void app_audio_fx_handle_input(button_event_t event) {
    if (event == BTN_LEFT) {
        if (selected_band > 0) selected_band--;
        needs_redraw = true;
    } else if (event == BTN_RIGHT) {
        if (selected_band < 9) selected_band++;
        needs_redraw = true;
    } else if (event == BTN_UP) {
        if (eq_gains[selected_band] < 12) {
            eq_gains[selected_band]++;
            check_custom_preset();
            apply_eq_gains();
            needs_redraw = true;
        }
    } else if (event == BTN_DOWN) {
        if (eq_gains[selected_band] > -13) {
            eq_gains[selected_band]--;
            check_custom_preset();
            apply_eq_gains();
            needs_redraw = true;
        }
    } else if (event == BTN_ENTER) {
        current_preset = (current_preset == 255) ? 0 : ((current_preset + 1) % NUM_PRESETS);
        memcpy(eq_gains, eq_presets[current_preset].gains, sizeof(eq_gains));
        apply_eq_gains();
        needs_redraw = true;
    } else if (event == BTN_A) {
        dsp_enabled = !dsp_enabled;
        needs_redraw = true;
    } else if (event == BTN_B) {
        speed_index = (speed_index + 1) % NUM_SPEEDS;
        needs_redraw = true;
    }
}

static void draw_ui(void) {
    char buf[64];

    // Header
    rg_gui_draw_text_box(0, 10, 240, 20, 0x0000, "AUDIO FX");

    // Preset
    snprintf(buf, sizeof(buf), "Preset: [ %s ]", current_preset == 255 ? "CUSTOM" : eq_presets[current_preset].name);
    rg_gui_draw_text_box(0, 40, 240, 20, 0x0000, buf);

    // Labels
    rg_gui_draw_text_box(0, 70, 240, 16, 0x0000, " 31  62 125 250 500 1K 2K 4K 8K 16");

    // EQ Bars
    int start_x = 10;
    int center_y = 155;
    
    // Draw center line
    rg_gui_draw_rect(0, center_y, 240, 2, RG_COLOR_RGB(100, 100, 100));
    rg_display_drain();

    for (int i = 0; i < 10; i++) {
        int x = start_x + i * 22;
        int h = eq_gains[i] * 5; // pixels per dB
        
        // Clear bar area (18x120)
        rg_gui_draw_rect(x, center_y - 60, 18, 120, 0x0000);
        
        uint16_t color;
        if (eq_gains[i] > 0) color = RG_COLOR_RGB(0, 255, 0);
        else if (eq_gains[i] < 0) color = RG_COLOR_RGB(255, 0, 0);
        else color = RG_COLOR_RGB(150, 150, 150);
        
        if (h > 0) {
            rg_gui_draw_rect(x, center_y - h, 18, h, color);
        } else if (h < 0) {
            rg_gui_draw_rect(x, center_y + 2, 18, -h, color);
        } else {
            rg_gui_draw_rect(x, center_y - 2, 18, 4, color);
        }
        
        // Selection highlight
        if (i == selected_band) {
            rg_gui_draw_rect(x, center_y + 65, 18, 4, RG_COLOR_WHITE);
        } else {
            rg_gui_draw_rect(x, center_y + 65, 18, 4, 0x0000);
        }
        rg_display_drain();
    }

    // Selected Band Info
    const char *band_names[] = {"31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"};
    snprintf(buf, sizeof(buf), "< Band: %s   %+d dB >", band_names[selected_band], eq_gains[selected_band]);
    rg_gui_draw_text_box(0, 240, 240, 20, 0x0000, buf);

    // DSP Toggle & Speed
    snprintf(buf, sizeof(buf), "[%s] Speed: %.2fx", dsp_enabled ? "ON" : "OFF", speed_values[speed_index]);
    rg_gui_draw_text_box(0, 270, 240, 20, 0x0000, buf);

    // Hints
    rg_gui_draw_text_box(0, 300, 240, 16, 0x0000, "UP/DN: Gain  L/R: Band");
    rg_display_drain();
}

void app_audio_fx_tick(void) {
    if (needs_redraw) {
        draw_ui();
        needs_redraw = false;
    }
}

int dsp_process_pcm(unsigned char *pcm_buf, int len, int sample_rate, int channels) {
    if (len <= 0) return len;

    if (sample_rate != current_sample_rate && sample_rate > 0) {
        esp_equalizer_uninit(eq_handle);
        eq_handle = esp_equalizer_init(channels, sample_rate, 10, 0);
        current_sample_rate = sample_rate;
        apply_eq_gains();
    }

    int processed_len = 0;
    if (dsp_enabled && eq_handle) {
        processed_len = esp_equalizer_process(eq_handle, pcm_buf, len, sample_rate, channels);
    } else {
        processed_len = len; // Pass-through
    }

    // Simple Speed Control (Pitch+Speed)
    float speed = speed_values[speed_index];
    if (speed != 1.0f && channels == 2) {
        int16_t *samples = (int16_t *)pcm_buf;
        int num_frames = processed_len / 4; // 2 channels * 2 bytes
        int new_frames = 0;
        
        if (speed == 0.75f) { // Repeat every 3rd frame
            int16_t temp[4000]; // Max buffer is 4000 bytes = 1000 frames
            if (num_frames > 1000) num_frames = 1000;
            int out_idx = 0;
            for (int i = 0; i < num_frames; i++) {
                temp[out_idx * 2] = samples[i * 2];
                temp[out_idx * 2 + 1] = samples[i * 2 + 1];
                out_idx++;
                if (i % 3 == 2 && out_idx < 1000) {
                    temp[out_idx * 2] = samples[i * 2];
                    temp[out_idx * 2 + 1] = samples[i * 2 + 1];
                    out_idx++;
                }
            }
            memcpy(samples, temp, out_idx * 4);
            processed_len = out_idx * 4;
        } else if (speed == 1.25f) { // Drop every 4th frame
            int out_idx = 0;
            for (int i = 0; i < num_frames; i++) {
                if (i % 4 != 3) {
                    samples[out_idx * 2] = samples[i * 2];
                    samples[out_idx * 2 + 1] = samples[i * 2 + 1];
                    out_idx++;
                }
            }
            processed_len = out_idx * 4;
        } else if (speed == 1.5f) { // Drop every 2nd frame
            int out_idx = 0;
            for (int i = 0; i < num_frames; i++) {
                if (i % 2 == 0) {
                    samples[out_idx * 2] = samples[i * 2];
                    samples[out_idx * 2 + 1] = samples[i * 2 + 1];
                    out_idx++;
                }
            }
            processed_len = out_idx * 4;
        }
    }

    return processed_len;
}

void apply_software_volume(unsigned char *pcm_buf, int len, int channels) {
    if (len > 0 && channels > 0) {
        float multiplier = (float)global_volume / 100.0f;
        // Apply cubic curve for natural logarithmic volume response
        multiplier = multiplier * multiplier * multiplier;
        
        int16_t *samples = (int16_t *)pcm_buf;
        int num_samples = len / 2; // 16-bit samples
        for (int i = 0; i < num_samples; i++) {
            int32_t val = (int32_t)(samples[i] * multiplier);
            if (val > 32767) val = 32767;
            if (val < -32768) val = -32768;
            samples[i] = (int16_t)val;
        }
    }
}

bool dsp_is_enabled(void) {
    return dsp_enabled;
}
