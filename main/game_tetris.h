#ifndef GAME_TETRIS_H
#define GAME_TETRIS_H

#include "input_manager.h"
#include <stdbool.h>

void game_tetris_start(void);
void game_tetris_tick(void);
bool game_tetris_input(button_event_t event);

#endif // GAME_TETRIS_H
