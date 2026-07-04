#ifndef RG_AUDIO_H
#define RG_AUDIO_H

#include <stdint.h>

void rg_audio_init(int sample_rate);
void rg_audio_submit(const int16_t *stereo_buffer, int frames);
void rg_audio_set_volume(int volume);
int rg_audio_get_volume(void);

#endif
