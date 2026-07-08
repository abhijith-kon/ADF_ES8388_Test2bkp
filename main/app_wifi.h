#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "input_manager.h"
#include <stdbool.h>

void app_wifi_init(void);
void app_wifi_start(void);
void app_wifi_stop(void);
void app_wifi_handle_input(button_event_t event);
void app_wifi_tick(void);

#endif // APP_WIFI_H
