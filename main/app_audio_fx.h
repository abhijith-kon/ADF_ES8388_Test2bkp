#ifndef APP_AUDIO_FX_H
#define APP_AUDIO_FX_H

#include "input_manager.h"
#include <stdbool.h>

// Lifecycle
void app_audio_fx_init(void);
void app_audio_fx_start(void);
void app_audio_fx_stop(void);
void app_audio_fx_handle_input(button_event_t event);
void app_audio_fx_tick(void);

// Cross-app API: called from app_music.c's decoder_write_cb
// Processes PCM buffer in-place. Returns processed length.
int dsp_process_pcm(unsigned char *pcm_buf, int len, int sample_rate, int channels);
void apply_software_volume(unsigned char *pcm_buf, int len, int channels);

// Query if DSP is globally enabled
bool dsp_is_enabled(void);

#endif // APP_AUDIO_FX_H
