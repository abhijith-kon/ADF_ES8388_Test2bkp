#ifndef GAME_2048_H
#define GAME_2048_H

#include "input_manager.h"
#include <stdbool.h>

void game_2048_start(void);
void game_2048_tick(void);
bool game_2048_input(button_event_t event);

#endif // GAME_2048_H
