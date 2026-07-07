#ifndef GAME_PONG_H
#define GAME_PONG_H

#include "input_manager.h"
#include <stdbool.h>

void game_pong_start(void);
void game_pong_tick(void);
bool game_pong_input(button_event_t event);

#endif // GAME_PONG_H
