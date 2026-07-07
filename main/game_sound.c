#include "game_sound.h"
#include "app_alarm.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile bool s_sound_playing = false;

static void set_tone(uint32_t freq, bool on) {
    if (app_alarm_is_ringing()) return;
    if (on && freq > 0) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512); // 50% duty on 10-bit timer
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    } else {
        if (!app_alarm_is_ringing()) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        }
    }
}

void game_sound_play_blip(uint32_t freq, uint32_t duration_ms) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    set_tone(freq, true);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_tone(0, false);
    s_sound_playing = false;
}

static void tetris_score_task(void *arg) {
    // Super Mario Coin / Line Clear sound (B5 -> E6)
    uint32_t notes[] = {988, 1319};
    uint32_t durs[] = {80, 240};
    for (int i = 0; i < 2; i++) {
        if (app_alarm_is_ringing()) break;
        set_tone(notes[i], true);
        vTaskDelay(pdMS_TO_TICKS(durs[i]));
    }
    set_tone(0, false);
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_score_tetris(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(tetris_score_task, "snd_tetris_sc", 2048, NULL, 5, NULL);
}

static void mario_game_over_melody(void) {
    // Super Mario Game Over theme
    uint32_t notes[] = {523, 392, 330, 440, 494, 440, 415, 466, 415, 392};
    uint32_t durs[] = {150, 150, 150, 150, 150, 150, 150, 150, 150, 450};
    for (int i = 0; i < 10; i++) {
        if (app_alarm_is_ringing()) break;
        set_tone(notes[i], true);
        vTaskDelay(pdMS_TO_TICKS(durs[i]));
        if (i < 9) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void tetris_gameover_task(void *arg) {
    mario_game_over_melody();
    set_tone(0, false);
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_gameover_tetris(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(tetris_gameover_task, "snd_tetris_go", 2048, NULL, 5, NULL);
}

static void game_2048_milestone_task(void *arg) {
    // Super Mario Power Up jingle
    uint32_t notes[] = {392, 494, 587, 784, 988, 1175, 415, 523, 622, 831, 1047, 1245};
    uint32_t durs[] = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 150};
    for (int i = 0; i < 12; i++) {
        if (app_alarm_is_ringing()) break;
        set_tone(notes[i], true);
        vTaskDelay(pdMS_TO_TICKS(durs[i]));
    }
    set_tone(0, false);
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_milestone_2048(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(game_2048_milestone_task, "snd_2048_ms", 2048, NULL, 5, NULL);
}

static void game_2048_gameover_task(void *arg) {
    mario_game_over_melody();
    set_tone(0, false);
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_gameover_2048(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(game_2048_gameover_task, "snd_2048_go", 2048, NULL, 5, NULL);
}

static void level_up_task(void *arg) {
    // Famous Super Mario 1-UP theme (E6, G6, E7, C7, D7, G7)
    uint32_t notes[] = {1319, 1568, 2637, 2093, 2349, 3136};
    uint32_t durs[] = {80, 80, 80, 80, 80, 200};
    for (int i = 0; i < 6; i++) {
        if (app_alarm_is_ringing()) break;
        set_tone(notes[i], true);
        vTaskDelay(pdMS_TO_TICKS(durs[i]));
    }
    set_tone(0, false);
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_level_up(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(level_up_task, "snd_lvl_up", 2048, NULL, 5, NULL);
}

static void game_over_task(void *arg) {
    mario_game_over_melody();
    set_tone(0, false);
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_game_over(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(game_over_task, "snd_game_over", 2048, NULL, 5, NULL);
}
