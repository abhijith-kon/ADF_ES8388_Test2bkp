#include "input_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "board_pins_config.h"
#include "esp_log.h"
#include "esp_timer.h"

/*
 * Input Manager — 74HC165 Shift Register + KY-040 Rotary Encoder
 *
 * 74HC165 (active LOW buttons):
 *   PL  = GPIO 7  (Parallel Load / Latch)
 *   CLK = GPIO 3  (Clock)
 *   QH  = GPIO 6  (Serial Data Out)
 *
 * Button mapping (active LOW — 0 = pressed):
 *   D7 = BTN_A       D3 = BTN_B
 *   D6 = BTN_UP      D2 = BTN_ESCAPE
 *   D5 = BTN_DOWN    D1 = BTN_ENTER
 *   D4 = BTN_LEFT    D0 = BTN_RIGHT
 *
 * KY-040 Rotary Encoder:
 *   CLK = GPIO 1, DT = GPIO 2
 *   CW rotation = BTN_VOL_UP, CCW = BTN_VOL_DOWN
 */

static const char *TAG = "INPUT";

// Encoder state
static int encoder_pos = 0;
static int last_clk = 1;

// Debounce: last reported button state and timestamp
static uint8_t last_button_state = 0xFF;
static int64_t last_button_time = 0;
#define DEBOUNCE_US 250000  // 250ms debounce

static uint8_t read_shift_register(void)
{
    // 1. Latch parallel inputs
    gpio_set_level(SR_PL_PIN, 0);
    esp_rom_delay_us(5);
    gpio_set_level(SR_PL_PIN, 1);

    // 2. Shift out 8 bits (MSB first: D7 comes out first)
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        int bit = gpio_get_level(SR_QH_PIN);
        data |= (bit << (7 - i));

        gpio_set_level(SR_CLK_PIN, 1);
        esp_rom_delay_us(5);
        gpio_set_level(SR_CLK_PIN, 0);
        esp_rom_delay_us(5);
    }

    return data;
}

void input_manager_init(void)
{
    gpio_config_t io_conf = {0};

    // Audio Amplifier Enable (PA_EN) on pin 48
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_48);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_48, 1); // 1 = Enable speaker/amplifier

    // 74HC165 outputs (PL, CLK)
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << SR_PL_PIN) | (1ULL << SR_CLK_PIN);
    gpio_config(&io_conf);
    gpio_set_level(SR_PL_PIN, 1);  // Latch idle HIGH
    gpio_set_level(SR_CLK_PIN, 0); // Clock idle LOW

    // 74HC165 input (QH)
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pin_bit_mask = (1ULL << SR_QH_PIN);
    gpio_config(&io_conf);

    // KY-040 Rotary Encoder
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = (1ULL << ENC_CLK_PIN) | (1ULL << ENC_DT_PIN);
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Input manager initialized (74HC165 + KY-040 encoder)");
}

button_event_t input_manager_get_event(void)
{
    static int last_enc_pos = 0;

    // --- 74HC165 Shift Register (buttons) ---
    uint8_t sr = read_shift_register();
    button_event_t evt = BTN_NONE;

    if (sr != 0xFF) {
        int64_t now = esp_timer_get_time();
        // Debounce: only report if state changed or enough time passed
        if (sr != last_button_state || (now - last_button_time) > DEBOUNCE_US) {
            last_button_state = sr;
            last_button_time = now;

            // Check each bit (active LOW). Return first detected press.
            if (!(sr & (1 << 7))) evt = BTN_A;
            else if (!(sr & (1 << 6))) evt = BTN_UP;
            else if (!(sr & (1 << 5))) evt = BTN_DOWN;
            else if (!(sr & (1 << 4))) evt = BTN_LEFT;
            else if (!(sr & (1 << 3))) evt = BTN_B;
            else if (!(sr & (1 << 2))) evt = BTN_ESCAPE;
            else if (!(sr & (1 << 1))) evt = BTN_ENTER;
            else if (!(sr & (1 << 0))) evt = BTN_RIGHT;
        }
    } else {
        last_button_state = 0xFF;  // Reset when all released
    }

    // --- KY-040 Rotary Encoder ---
    int clk = gpio_get_level(ENC_CLK_PIN);
    int dt = gpio_get_level(ENC_DT_PIN);
    static int64_t last_enc_time = 0;
    int64_t now_us = esp_timer_get_time();
    
    if (clk != last_clk && clk == 0) {
        if ((now_us - last_enc_time) > 20000) { // 20ms debounce
            if (dt == 1) {
                encoder_pos++;
            } else {
                encoder_pos--;
            }
            last_enc_time = now_us;
        }
    }
    last_clk = clk;

    if (encoder_pos > last_enc_pos) { last_enc_pos = encoder_pos; evt = BTN_VOL_UP; }
    else if (encoder_pos < last_enc_pos) { last_enc_pos = encoder_pos; evt = BTN_VOL_DOWN; }

    if (evt != BTN_NONE) {
        // gpio_set_level(GPIO_NUM_48, 1);
        // esp_rom_delay_us(10000); // 10ms beep
        // gpio_set_level(GPIO_NUM_48, 0);
    }

    return evt;
}
