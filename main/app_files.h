#ifndef APP_FILES_H
#define APP_FILES_H

#include <stdbool.h>
#include "input_manager.h"

void app_files_init(void);
void app_files_start(void);
void app_files_stop(void);
void app_files_handle_input(button_event_t event);
void app_files_tick(void);
bool app_files_is_in_page_view(void);
bool app_files_is_in_rsvp_mode(void);

#endif // APP_FILES_H
