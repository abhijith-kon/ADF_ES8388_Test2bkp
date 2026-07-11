#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "input_manager.h"

void app_settings_init(void);
void app_settings_start(void);
void app_settings_stop(void);
void app_settings_handle_input(button_event_t event);
void app_settings_tick(void);
void neo_animation_tick(void);

#endif // APP_SETTINGS_H
