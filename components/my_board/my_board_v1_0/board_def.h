#ifndef _AUDIO_BOARD_DEFINITION_H_
#define _AUDIO_BOARD_DEFINITION_H_

#include "driver/gpio.h"

// Define BOARD_PA_GAIN here because ADF drivers expect it to be globally available via board_def.h
#ifndef BOARD_PA_GAIN
#define BOARD_PA_GAIN (10)
#endif

/**
 * @brief SDCARD GPIO configurations
 */
#define SDCARD_PWR_CTRL             -1
#define ESP_SD_PIN_CLK              38
#define ESP_SD_PIN_CMD              39
#define ESP_SD_PIN_D0               40
#define ESP_SD_PIN_D1               41
#define ESP_SD_PIN_D2               42
#define ESP_SD_PIN_D3               21
#define ESP_SD_PIN_D4               -1
#define ESP_SD_PIN_D5               -1
#define ESP_SD_PIN_D6               -1
#define ESP_SD_PIN_D7               -1
#define ESP_SD_PIN_CD               -1
#define ESP_SD_PIN_WP               -1
#define SDCARD_INTR_GPIO            -1

#define SDCARD_OPEN_FILE_NUM_MAX    5

#define BUTTON_VOLUP_ID             0
#define BUTTON_VOLDOWN_ID           1
#define BUTTON_MUTE_ID              2
#define BUTTON_SET_ID               3
#define BUTTON_MODE_ID              4
#define BUTTON_PLAY_ID              5

// These are needed by some ADF internal macros
#ifndef PA_ENABLE_GPIO
#define PA_ENABLE_GPIO              -1
#endif
#ifndef ADC_DETECT_GPIO
#define ADC_DETECT_GPIO             -1
#endif
#ifndef BATTERY_DETECT_GPIO
#define BATTERY_DETECT_GPIO         -1
#endif

/**
 * @brief I2S GPIO configurations
 */
typedef struct {
    int mck_io_num;     /*!< MCK gpio number — MUST be first to match i2s_std_gpio_config_t for memcpy */
    int bck_io_num;     /*!< BCK gpio number */
    int ws_io_num;      /*!< WS gpio number */
    int data_out_num;   /*!< DATA_OUT gpio number */
    int data_in_num;    /*!< DATA_IN gpio number */
} board_i2s_pin_t;

// Declaration only
int8_t get_sdcard_intr_gpio(void);

extern audio_hal_func_t AUDIO_NEW_CODEC_DEFAULT_HANDLE;

#define AUDIO_CODEC_DEFAULT_CONFIG(){                   \
        .adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,        \
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,         \
        .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH,        \
        .i2s_iface = {                                  \
            .mode = AUDIO_HAL_MODE_SLAVE,               \
            .fmt = AUDIO_HAL_I2S_NORMAL,                \
            .samples = AUDIO_HAL_48K_SAMPLES,           \
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,        \
        },                                              \
};

#endif
