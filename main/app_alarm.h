#ifndef APP_ALARM_H
#define APP_ALARM_H

#include "input_manager.h"
#include <stdbool.h>

void app_alarm_init(void);
void app_alarm_start(void);
void app_alarm_stop(void);
void app_alarm_handle_input(button_event_t event);
void app_alarm_tick(void);

bool app_alarm_is_in_ui(void);
bool app_alarm_is_ringing(void);
void app_alarm_silence(void);
void app_alarm_stop(void);

#endif
