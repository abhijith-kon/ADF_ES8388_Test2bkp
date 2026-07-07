#ifndef GAME_SOUND_H
#define GAME_SOUND_H

#include <stdint.h>
#include <stdbool.h>

void game_sound_play_blip(uint32_t freq, uint32_t duration_ms);
void game_sound_play_score_tetris(void);
void game_sound_play_gameover_tetris(void);
void game_sound_play_milestone_2048(void);
void game_sound_play_gameover_2048(void);
void game_sound_play_level_up(void);
void game_sound_play_game_over(void);

#endif // GAME_SOUND_H
