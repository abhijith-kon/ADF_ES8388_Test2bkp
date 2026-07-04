#include "rg_audio.h"
#include "audio_hal.h"
#include "board.h"
#include "esp_log.h"
// REMOVED driver/i2s.h to avoid legacy driver conflict

static const char *TAG = "rg_audio";

void rg_audio_init(int sample_rate)
{
    ESP_LOGI(TAG, "Initializing audio board codec...");
    audio_board_handle_t board_handle = audio_board_init();
    if (board_handle) {
        audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
        audio_hal_set_volume(board_handle->audio_hal, 60);
    }
    ESP_LOGI(TAG, "Audio Codec ready.");
}

void rg_audio_submit(const int16_t *stereo_buffer, int frames)
{
    // Emulators are not running in this phase, so this is empty.
    // Future integration will use the ADF pipeline or new esp_driver_i2s API here.
}

void rg_audio_set_volume(int volume)
{
    audio_board_handle_t board_handle = audio_board_get_handle();
    if (board_handle && board_handle->audio_hal) {
        audio_hal_set_volume(board_handle->audio_hal, volume);
    }
}

int rg_audio_get_volume(void)
{
    int volume = 0;
    audio_board_handle_t board_handle = audio_board_get_handle();
    if (board_handle && board_handle->audio_hal) {
        audio_hal_get_volume(board_handle->audio_hal, &volume);
    }
    return volume;
}
