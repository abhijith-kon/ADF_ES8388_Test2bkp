#ifndef APP_MUSIC_H
#define APP_MUSIC_H

#include "input_manager.h"
#include "audio_hal.h"

void app_music_init(audio_hal_handle_t hal_handle);
void app_music_start(void);
void app_music_stop(void);
void app_music_hide(void);
bool app_music_is_playing(void);
void app_music_bg_tick(void);
void app_music_handle_input(button_event_t event);
void app_music_tick(void);
bool app_music_is_in_player_ui(void);

#endif
