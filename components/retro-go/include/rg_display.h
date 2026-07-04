#ifndef RG_DISPLAY_H
#define RG_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RG_SCREEN_ROTATION_0,
    RG_SCREEN_ROTATION_90,
    RG_SCREEN_ROTATION_180,
    RG_SCREEN_ROTATION_270,
} rg_display_rotation_t;

typedef enum {
    RG_SCREEN_SCALING_FIT,
    RG_SCREEN_SCALING_STRETCH,
    RG_SCREEN_SCALING_CENTER,
} rg_display_scaling_t;

typedef struct {
    rg_display_scaling_t scaling;
    rg_display_rotation_t rotation;
    bool filter;
} rg_display_config_t;

void rg_display_init(void);
void rg_display_write(int x, int y, int width, int height, int stride, const void *buffer);
void rg_display_set_config(rg_display_config_t config);
void rg_display_set_backlight(float level);
void rg_display_drain(void);

#endif
