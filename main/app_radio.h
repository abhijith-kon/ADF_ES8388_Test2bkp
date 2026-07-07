#ifndef APP_RADIO_H
#define APP_RADIO_H

#include <stdbool.h>

void app_radio_start(void);
void app_radio_stop(void);
void app_radio_handle_input(int button_event);
void app_radio_tick(void);
bool app_radio_is_active(void);

#endif // APP_RADIO_H
