#include "game_sound.h"
#include "app_alarm.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile bool s_sound_playing = false;

static void set_tone(uint32_t freq, bool on) {
    if (app_alarm_is_ringing()) return;
    if (on && freq > 0) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = LEDC_CHANNEL_1,
            .timer_sel      = LEDC_TIMER_1,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = GPIO_NUM_46,
            .duty           = 512,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_channel);
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, freq);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 512); // 50% duty on 10-bit timer
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    } else {
        if (!app_alarm_is_ringing()) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
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

static void play_tone_ms(uint32_t freq, uint32_t dur_ms) {
    if (app_alarm_is_ringing()) return;
    if (freq > 0) {
        set_tone(freq, true);
    } else {
        set_tone(0, false);
    }
    vTaskDelay(pdMS_TO_TICKS(dur_ms));
    set_tone(0, false);
}

static void tetris_score_task(void *arg) {
    uint32_t notes[] = {1319, 1568, 2637, 2093, 2349, 3136};
    for (int i = 0; i < 6; i++) {
        if (app_alarm_is_ringing()) break;
        play_tone_ms(notes[i], 120);
    }
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_score_tetris(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(tetris_score_task, "snd_tetris_sc", 2048, NULL, 5, NULL);
}

static void tetris_gameover_task(void *arg) {
    if (app_alarm_is_ringing()) goto done;
    play_tone_ms(494, 150); 
    play_tone_ms(698, 300); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(698, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(698, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(659, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(587, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(523, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(330, 150); 
    play_tone_ms(262, 150); 
done:
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_gameover_tetris(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(tetris_gameover_task, "snd_tetris_go", 2048, NULL, 5, NULL);
}

static void game_2048_milestone_task(void *arg) {
    uint32_t notes[] = {1046, 1318, 1568, 2093};
    for (int i = 0; i < 4; i++) {
        if (app_alarm_is_ringing()) break;
        play_tone_ms(notes[i], 75);
    }
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_milestone_2048(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(game_2048_milestone_task, "snd_2048_ms", 2048, NULL, 5, NULL);
}

static void game_2048_gameover_task(void *arg) {
    uint32_t notes[] = {392, 330, 294, 261};
    for (int i = 0; i < 4; i++) {
        if (app_alarm_is_ringing()) break;
        play_tone_ms(notes[i], 200);
    }
    if (!app_alarm_is_ringing()) {
        play_tone_ms(130, 600);
    }
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_gameover_2048(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(game_2048_gameover_task, "snd_2048_go", 2048, NULL, 5, NULL);
}

static void level_up_task(void *arg) {
    uint32_t notes[] = {1319, 1568, 2637, 2093, 2349, 3136};
    for (int i = 0; i < 6; i++) {
        if (app_alarm_is_ringing()) break;
        play_tone_ms(notes[i], 120);
    }
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_level_up(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(level_up_task, "snd_lvl_up", 2048, NULL, 5, NULL);
}

static void game_over_task(void *arg) {
    if (app_alarm_is_ringing()) goto done;
    play_tone_ms(494, 150); 
    play_tone_ms(698, 300); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(698, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(698, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(659, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(587, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(523, 150); vTaskDelay(pdMS_TO_TICKS(50));
    play_tone_ms(330, 150); 
    play_tone_ms(262, 150); 
done:
    s_sound_playing = false;
    vTaskDelete(NULL);
}

void game_sound_play_game_over(void) {
    if (app_alarm_is_ringing() || s_sound_playing) return;
    s_sound_playing = true;
    xTaskCreate(game_over_task, "snd_game_over", 2048, NULL, 5, NULL);
}

